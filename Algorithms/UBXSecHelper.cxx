#include "cetlib/exception.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Framework/Principal/Handle.h"
#include "canvas/Persistency/Common/FindManyP.h"
#include "canvas/Persistency/Common/FindOneP.h"

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "canvas/Persistency/Common/FindManyP.h"


#include "lardataobj/AnalysisBase/CosmicTag.h"
#include "larcore/Geometry/Geometry.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/Cluster.h"
#include "lardataobj/RecoBase/PFParticle.h"
#include "lardataobj/RecoBase/Seed.h"
#include "lardataobj/RecoBase/Shower.h"
#include "lardataobj/RecoBase/SpacePoint.h"
#include "lardataobj/RecoBase/Track.h"
#include "lardataobj/RecoBase/Vertex.h"
#include "lardataobj/RecoBase/Wire.h"
#include "nusimdata/SimulationBase/MCParticle.h"
#include "nusimdata/SimulationBase/MCTruth.h"
#include "larcoreobj/SimpleTypesAndConstants/RawTypes.h"
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "larsim/MCCheater/BackTracker.h"

#include "larevt/CalibrationDBI/Interface/DetPedestalService.h"
#include "larevt/CalibrationDBI/Interface/DetPedestalProvider.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusService.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusProvider.h"

#include "lardataobj/Simulation/SimChannel.h"

#include "lardataobj/RecoBase/PFParticle.h"
//#include "larpandora/LArPandoraInterface/LArPandoraHelper.h"

#include "UBXSecHelper.h"



//___________________________________________________________________________________________________
void UBXSecHelper::GetTrackPurityAndEfficiency( lar_pandora::HitVector recoHits, double & trackPurity, double & trackEfficiency ) {

  art::ServiceHandle<cheat::BackTracker> bt;

  // map from geant track id to true track deposited energy
  std::map<int,double> trkidToIDE;

  for(size_t h = 0; h < recoHits.size(); h++){

    art::Ptr<recob::Hit> recoHit = recoHits[h];
    std::vector<sim::TrackIDE> eveIDs = bt->HitToEveID(recoHit);

    for(size_t e = 0; e < eveIDs.size(); ++e){
      //std::cout<<"[Hit "<< h<<"] hit plane: "<<recoHit->View()<<" "<<e<<" "<<eveIDs[e].trackID<<" "<<eveIDs[e].energy<<" "<<eveIDs[e].energyFrac<<"   pdg "<< (bt->TrackIDToParticle(eveIDs[e].trackID))->PdgCode()<<"   process "<<(bt->TrackIDToParticle(eveIDs[e].trackID))->Process()<<std::endl;
      trkidToIDE[eveIDs[e].trackID] += eveIDs[e].energy;
    }
  }

  double maxe = -1;
  double tote = 0;
  int trackid;
  for(auto const& ii : trkidToIDE){
    tote += ii.second;
    if ((ii.second)>maxe){
      maxe = ii.second;
      trackid = ii.first;
    }
  }

  if (tote>0){
    trackPurity = maxe/tote;
  }

  std::vector<sim::IDE> vide(bt->TrackIDToSimIDE(trackid)); 
  double totalEnergyFromMainTrack = 0;
  for (const sim::IDE& ide: vide) {
    totalEnergyFromMainTrack += ide.energy;
  }

  trackEfficiency = maxe/(totalEnergyFromMainTrack); //totalEnergyFromMainTrack includes both inductions and collection energies

  return;
}



//___________________________________________________________________________________________________
void UBXSecHelper::GetRecoToTrueMatches(art::Event const & e,
                                           std::string _pfp_producer,
                                           std::string _spacepointLabel,
                                           std::string _geantModuleLabel,
                                           std::string _hitfinderLabel,
                                           lar_pandora::MCParticlesToPFParticles &matchedParticles,
                                           lar_pandora::MCParticlesToHits &matchedParticleHits)
{

   bool _debug = true; 

  // --- Collect hits
  lar_pandora::HitVector hitVector;
  lar_pandora::LArPandoraHelper::CollectHits(e, _hitfinderLabel, hitVector);
 
   // --- Collect PFParticles and match Reco Particles to Hits
  lar_pandora::PFParticleVector  recoParticleVector;
  lar_pandora::PFParticleVector  recoNeutrinoVector;
  lar_pandora::PFParticlesToHits recoParticlesToHits;
  lar_pandora::HitsToPFParticles recoHitsToParticles;

  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _pfp_producer, recoParticleVector);
  lar_pandora::LArPandoraHelper::SelectNeutrinoPFParticles(recoParticleVector, recoNeutrinoVector);
  lar_pandora::LArPandoraHelper::BuildPFParticleHitMaps(e, _pfp_producer, _spacepointLabel, recoParticlesToHits, recoHitsToParticles, lar_pandora::LArPandoraHelper::kAddDaughters);

  if (_debug) {
    std::cout << "  RecoNeutrinos: " << recoNeutrinoVector.size() << std::endl;
    std::cout << "  RecoParticles: " << recoParticleVector.size() << std::endl;
  }

  // --- Collect MCParticles and match True Particles to Hits
  lar_pandora::MCParticleVector     trueParticleVector;
  lar_pandora::MCTruthToMCParticles truthToParticles;
  lar_pandora::MCParticlesToMCTruth particlesToTruth;
  lar_pandora::MCParticlesToHits    trueParticlesToHits;
  lar_pandora::HitsToMCParticles    trueHitsToParticles;

  if (!e.isRealData()) {
    lar_pandora::LArPandoraHelper::CollectMCParticles(e, _geantModuleLabel, trueParticleVector);
    lar_pandora::LArPandoraHelper::CollectMCParticles(e, _geantModuleLabel, truthToParticles, particlesToTruth);
    lar_pandora::LArPandoraHelper::BuildMCParticleHitMaps(e, _geantModuleLabel, hitVector, trueParticlesToHits, trueHitsToParticles, lar_pandora::LArPandoraHelper::kAddDaughters);
  }

  if (_debug) {
    std::cout << "  TrueParticles: " << particlesToTruth.size() << std::endl;
    std::cout << "  TrueEvents: " << truthToParticles.size() << std::endl;
  }


  GetRecoToTrueMatches(recoParticlesToHits,
                       trueHitsToParticles,
                       matchedParticles,
                       matchedParticleHits);
}


//___________________________________________________________________________________________________
void UBXSecHelper::GetRecoToTrueMatches(const lar_pandora::PFParticlesToHits &recoParticlesToHits,
                                           const lar_pandora::HitsToMCParticles &trueHitsToParticles,
                                           lar_pandora::MCParticlesToPFParticles &matchedParticles,
                                           lar_pandora::MCParticlesToHits &matchedHits) 
{
    PFParticleSet recoVeto; MCParticleSet trueVeto;
    bool _recursiveMatching = false;

    GetRecoToTrueMatches(recoParticlesToHits, trueHitsToParticles, matchedParticles, matchedHits, recoVeto, trueVeto, _recursiveMatching);
}

//___________________________________________________________________________________________________
void UBXSecHelper::GetRecoToTrueMatches(const lar_pandora::PFParticlesToHits &recoParticlesToHits,
                                           const lar_pandora::HitsToMCParticles &trueHitsToParticles,
                                           lar_pandora::MCParticlesToPFParticles &matchedParticles,
                                           lar_pandora::MCParticlesToHits &matchedHits,
                                           PFParticleSet &vetoReco,
                                           MCParticleSet &vetoTrue,
                                           bool _recursiveMatching) 
{
    bool foundMatches(false);

    // Loop over the reco particles
    for (lar_pandora::PFParticlesToHits::const_iterator iter1 = recoParticlesToHits.begin(), iterEnd1 = recoParticlesToHits.end();
        iter1 != iterEnd1; ++iter1)
    {
        const art::Ptr<recob::PFParticle> recoParticle = iter1->first;
        if (vetoReco.count(recoParticle) > 0)
            continue;

        const lar_pandora::HitVector &hitVector = iter1->second;

        lar_pandora::MCParticlesToHits truthContributionMap;

        // Loop over all the hits associated to this reco particle
        for (lar_pandora::HitVector::const_iterator iter2 = hitVector.begin(), iterEnd2 = hitVector.end(); iter2 != iterEnd2; ++iter2)
        {
            const art::Ptr<recob::Hit> hit = *iter2;

            lar_pandora::HitsToMCParticles::const_iterator iter3 = trueHitsToParticles.find(hit);
            if (trueHitsToParticles.end() == iter3)
                continue;

            const art::Ptr<simb::MCParticle> trueParticle = iter3->second;
            if (vetoTrue.count(trueParticle) > 0)
                continue;

            // This map will contain all the true particles that match some or all of the hits of the reco particle
            truthContributionMap[trueParticle].push_back(hit);
        }

        // Now we want to find the true particle that has more hits in common with this reco particle than the others
        lar_pandora::MCParticlesToHits::const_iterator mIter = truthContributionMap.end();

        for (lar_pandora::MCParticlesToHits::const_iterator iter4 = truthContributionMap.begin(), iterEnd4 = truthContributionMap.end();
            iter4 != iterEnd4; ++iter4)
        {
            if ((truthContributionMap.end() == mIter) || (iter4->second.size() > mIter->second.size()))
            {
                mIter = iter4;
            }
        }

        if (truthContributionMap.end() != mIter)
        {
            const art::Ptr<simb::MCParticle> trueParticle = mIter->first;

            lar_pandora::MCParticlesToHits::const_iterator iter5 = matchedHits.find(trueParticle);

            if ((matchedHits.end() == iter5) || (mIter->second.size() > iter5->second.size()))
            {
                matchedParticles[trueParticle] = recoParticle;
                matchedHits[trueParticle] = mIter->second;
                foundMatches = true;
            }
        }
    } // recoParticlesToHits loop ends

    if (!foundMatches)
        return;

    for (lar_pandora::MCParticlesToPFParticles::const_iterator pIter = matchedParticles.begin(), pIterEnd = matchedParticles.end();
        pIter != pIterEnd; ++pIter)
    {
        vetoTrue.insert(pIter->first);
        vetoReco.insert(pIter->second);
    }

    if (_recursiveMatching)
        GetRecoToTrueMatches(recoParticlesToHits, trueHitsToParticles, matchedParticles, matchedHits, vetoReco, vetoTrue, _recursiveMatching);
}


//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(art::Event const & e, 
                                    std::string _particleLabel, 
                                    std::vector<lar_pandora::PFParticleVector> & pfp_v_v, 
                                    std::vector<lar_pandora::TrackVector> & track_v_v){

  //Vectors and maps we will use to store Pandora information
  lar_pandora::PFParticleVector pfParticleList;              //vector of PFParticles
  lar_pandora::PFParticlesToClusters pfParticleToClusterMap; //PFParticle-to-cluster map

  //Use LArPandoraHelper functions to collect Pandora information
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, pfParticleList, pfParticleToClusterMap); //collect PFParticles and build map PFParticles to Clusters

  // Collect vertices and tracks
  lar_pandora::VertexVector           allPfParticleVertices;
  lar_pandora::PFParticlesToVertices  pfParticleToVertexMap;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, allPfParticleVertices, pfParticleToVertexMap);
  lar_pandora::TrackVector            allPfParticleTracks;
  lar_pandora::PFParticlesToTracks    pfParticleToTrackMap;
  lar_pandora::LArPandoraHelper::CollectTracks(e, _particleLabel, allPfParticleTracks, pfParticleToTrackMap);

  GetTPCObjects(pfParticleList, pfParticleToTrackMap, pfParticleToVertexMap, pfp_v_v, track_v_v);
}

//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(art::Event const & e,
                                    std::string _particleLabel,
                                    std::string _trackLabel,
                                    std::vector<lar_pandora::PFParticleVector> & pfp_v_v,
                                    std::vector<lar_pandora::TrackVector> & track_v_v){

  //Vectors and maps we will use to store Pandora information
  lar_pandora::PFParticleVector pfParticleList;              //vector of PFParticles
  lar_pandora::PFParticlesToClusters pfParticleToClusterMap; //PFParticle-to-cluster map

  //Use LArPandoraHelper functions to collect Pandora information
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, pfParticleList, pfParticleToClusterMap); //collect PFParticles and build map PFParticles to Clusters

  // Collect vertices and tracks
  lar_pandora::VertexVector           allPfParticleVertices;
  lar_pandora::PFParticlesToVertices  pfParticleToVertexMap;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, allPfParticleVertices, pfParticleToVertexMap);
  lar_pandora::TrackVector            allPfParticleTracks;
  lar_pandora::PFParticlesToTracks    pfParticleToTrackMap;
  lar_pandora::LArPandoraHelper::CollectTracks(e, _trackLabel, allPfParticleTracks, pfParticleToTrackMap);

  GetTPCObjects(pfParticleList, pfParticleToTrackMap, pfParticleToVertexMap, pfp_v_v, track_v_v);
}

//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(art::Event const & e,
                                    std::string _particleLabel,
                                    std::string _trackLabel,
                                    std::vector<lar_pandora::PFParticleVector> & pfp_v_v,
                                    std::vector<lar_pandora::TrackVector> & track_v_v,
                                    lar_pandora::PFParticlesToSpacePoints & pfp_to_spacept,
                                    lar_pandora::SpacePointsToHits & spacept_to_hits){

  //Vectors and maps we will use to store Pandora information
  lar_pandora::PFParticleVector pfParticleList;              //vector of PFParticles
  lar_pandora::PFParticlesToClusters pfParticleToClusterMap; //PFParticle-to-cluster map

  //Use LArPandoraHelper functions to collect Pandora information
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, pfParticleList, pfParticleToClusterMap); //collect PFParticles and build map PFParticles to Clusters

  // Collect vertices and tracks
  lar_pandora::VertexVector           allPfParticleVertices;
  lar_pandora::PFParticlesToVertices  pfParticleToVertexMap;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, allPfParticleVertices, pfParticleToVertexMap);
  lar_pandora::TrackVector            allPfParticleTracks;
  lar_pandora::PFParticlesToTracks    pfParticleToTrackMap;
  lar_pandora::LArPandoraHelper::CollectTracks(e, _trackLabel, allPfParticleTracks, pfParticleToTrackMap);

  // Also collect spacepoints in this case
  lar_pandora::PFParticleVector temp2;
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, temp2, pfp_to_spacept);

  std::cout << "[UBXSecHelper] Looping over pfp_to_spacept map:" << std::endl;
  for (auto const& iter : pfp_to_spacept) {
    std::cout << "[UBXSecHelper]   pfp id: " <<  (iter.first)->Self() << ", number of spacepoints: " << (iter.second).size() << std::endl;
  }

  lar_pandora::SpacePointVector temp3;
  lar_pandora::LArPandoraHelper::CollectSpacePoints (e, _particleLabel, temp3, spacept_to_hits);
   

  GetTPCObjects(pfParticleList, pfParticleToTrackMap, pfParticleToVertexMap, pfp_v_v, track_v_v);

}

//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(art::Event const & e, 
                                    std::string _particleLabel, 
                                    std::vector<lar_pandora::PFParticleVector> & pfp_v_v, 
                                    std::vector<lar_pandora::ShowerVector> & shower_v_v){

  //Vectors and maps we will use to store Pandora information
  lar_pandora::PFParticleVector pfParticleList;              //vector of PFParticles
  lar_pandora::PFParticlesToClusters pfParticleToClusterMap; //PFParticle-to-cluster map

  //Use LArPandoraHelper functions to collect Pandora information
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, pfParticleList, pfParticleToClusterMap); //collect PFParticles and build map PFParticles to Clusters

  // Collect vertices and tracks
  lar_pandora::VertexVector           allPfParticleVertices;
  lar_pandora::PFParticlesToVertices  pfParticleToVertexMap;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, allPfParticleVertices, pfParticleToVertexMap);
  lar_pandora::ShowerVector            allPfParticleShowers;
  lar_pandora::PFParticlesToShowers    pfParticleToShowerMap;
  lar_pandora::LArPandoraHelper::CollectShowers(e, _particleLabel, allPfParticleShowers, pfParticleToShowerMap);

  GetTPCObjects(pfParticleList, pfParticleToShowerMap, pfParticleToVertexMap, pfp_v_v, shower_v_v);
}

//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(art::Event const & e,
                                    std::string _particleLabel,
                                    std::string _showerLabel,
                                    std::vector<lar_pandora::PFParticleVector> & pfp_v_v,
                                    std::vector<lar_pandora::ShowerVector> & shower_v_v){

  //Vectors and maps we will use to store Pandora information
  lar_pandora::PFParticleVector pfParticleList;              //vector of PFParticles
  lar_pandora::PFParticlesToClusters pfParticleToClusterMap; //PFParticle-to-cluster map

  //Use LArPandoraHelper functions to collect Pandora information
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, pfParticleList, pfParticleToClusterMap); //collect PFParticles and build map PFParticles to Clusters

  // Collect vertices and tracks
  lar_pandora::VertexVector           allPfParticleVertices;
  lar_pandora::PFParticlesToVertices  pfParticleToVertexMap;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, allPfParticleVertices, pfParticleToVertexMap);
  lar_pandora::ShowerVector            allPfParticleShowers;
  lar_pandora::PFParticlesToShowers    pfParticleToShowerMap;
  lar_pandora::LArPandoraHelper::CollectShowers(e, _showerLabel, allPfParticleShowers, pfParticleToShowerMap);

  GetTPCObjects(pfParticleList, pfParticleToShowerMap, pfParticleToVertexMap, pfp_v_v, shower_v_v);
}

//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(art::Event const & e,
                                    std::string _particleLabel,
                                    std::string _showerLabel,
                                    std::vector<lar_pandora::PFParticleVector> & pfp_v_v,
                                    std::vector<lar_pandora::ShowerVector> & shower_v_v,
                                    lar_pandora::PFParticlesToSpacePoints & pfp_to_spacept,
                                    lar_pandora::SpacePointsToHits & spacept_to_hits){

  //Vectors and maps we will use to store Pandora information
  lar_pandora::PFParticleVector pfParticleList;              //vector of PFParticles
  lar_pandora::PFParticlesToClusters pfParticleToClusterMap; //PFParticle-to-cluster map

  //Use LArPandoraHelper functions to collect Pandora information
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, pfParticleList, pfParticleToClusterMap); //collect PFParticles and build map PFParticles to Clusters

  // Collect vertices and tracks
  lar_pandora::VertexVector           allPfParticleVertices;
  lar_pandora::PFParticlesToVertices  pfParticleToVertexMap;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, allPfParticleVertices, pfParticleToVertexMap);
  lar_pandora::ShowerVector            allPfParticleShowers;
  lar_pandora::PFParticlesToShowers    pfParticleToShowerMap;
  lar_pandora::LArPandoraHelper::CollectShowers(e, _showerLabel, allPfParticleShowers, pfParticleToShowerMap);

  // Also collect spacepoints in this case
  lar_pandora::PFParticleVector temp2;
  lar_pandora::LArPandoraHelper::CollectPFParticles(e, _particleLabel, temp2, pfp_to_spacept);

  std::cout << "[UBXSecHelper] Looping over pfp_to_spacept map:" << std::endl;
  for (auto const& iter : pfp_to_spacept) {
    std::cout << "[UBXSecHelper]   pfp id: " <<  (iter.first)->Self() << ", number of spacepoints: " << (iter.second).size() << std::endl;
  }

  lar_pandora::SpacePointVector temp3;
  lar_pandora::LArPandoraHelper::CollectSpacePoints (e, _particleLabel, temp3, spacept_to_hits);
   

  GetTPCObjects(pfParticleList, pfParticleToShowerMap, pfParticleToVertexMap, pfp_v_v, shower_v_v);

}

//____________________________________________________________
void UBXSecHelper::GetNuVertexFromTPCObject(art::Event const & e, 
                                               std::string _particleLabel,
                                               lar_pandora::PFParticleVector pfp_v, 
                                               double *reco_nu_vtx){

  reco_nu_vtx[0] = -9999;
  reco_nu_vtx[1] = -9999;
  reco_nu_vtx[2] = -9999;

  lar_pandora::VertexVector          vertexVector;
  lar_pandora::PFParticlesToVertices particlesToVertices;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, vertexVector, particlesToVertices);

  for(unsigned int pfp = 0; pfp < pfp_v.size(); pfp++){

    if(lar_pandora::LArPandoraHelper::IsNeutrino(pfp_v.at(pfp))) {

      lar_pandora::VertexVector vertex_v = particlesToVertices.find(pfp_v.at(pfp))->second;
      if (vertex_v.size() > 1)
        std::cout << "More than one vertex associated to neutrino PFP!" << std::endl;
      else if (vertex_v.size() == 0)
        std::cout << "Zero vertices associated to neutrino PFP!" << std::endl;
      else {
       vertex_v[0]->XYZ(reco_nu_vtx);
       break;
      }
    }
  }

}

//____________________________________________________________
void UBXSecHelper::GetNuVertexFromTPCObject(art::Event const & e,
                                               std::string _particleLabel,
                                               lar_pandora::PFParticleVector pfp_v,
                                               recob::Vertex & reco_nu_vtx){


  lar_pandora::VertexVector          vertexVector;
  lar_pandora::PFParticlesToVertices particlesToVertices;
  lar_pandora::LArPandoraHelper::CollectVertices(e, _particleLabel, vertexVector, particlesToVertices);

  for(unsigned int pfp = 0; pfp < pfp_v.size(); pfp++){

    if(lar_pandora::LArPandoraHelper::IsNeutrino(pfp_v.at(pfp))) {

      lar_pandora::VertexVector vertex_v = particlesToVertices.find(pfp_v.at(pfp))->second;
      if (vertex_v.size() > 1)
        std::cout << "More than one vertex associated to neutrino PFP!" << std::endl;
      else if (vertex_v.size() == 0)
        std::cout << "Zero vertices associated to neutrino PFP!" << std::endl;
      else {
        reco_nu_vtx = *(vertex_v[0]);
        double xyz[3];
        reco_nu_vtx.XYZ(xyz);
        break;
      }
    }
  }
}

//_________________________________________________________________
art::Ptr<recob::PFParticle> UBXSecHelper::GetNuPFP(lar_pandora::PFParticleVector pfp_v){

  for (unsigned int pfp = 0; pfp < pfp_v.size(); pfp++) {

    if(lar_pandora::LArPandoraHelper::IsNeutrino(pfp_v.at(pfp))) {
      return pfp_v.at(pfp);
    }
  }
  std::cout << "No neutrino PFP found." << std::endl;

  art::Ptr<recob::PFParticle> temp;
  return temp;

}




//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(lar_pandora::PFParticleVector pfParticleList, 
                                 lar_pandora::PFParticlesToTracks pfParticleToTrackMap, 
                                 lar_pandora::PFParticlesToVertices  pfParticleToVertexMap, 
                                 std::vector<lar_pandora::PFParticleVector> & pfp_v_v, 
                                 std::vector<lar_pandora::TrackVector> & track_v_v) {

  track_v_v.clear();
  pfp_v_v.clear();

  std::cout << "[UBXSecHelper] Getting TPC Objects..." << std::endl;

  for (unsigned int n = 0; n < pfParticleList.size(); ++n) {
    const art::Ptr<recob::PFParticle> particle = pfParticleList.at(n);

    if(lar_pandora::LArPandoraHelper::IsNeutrino(particle)) {
      std::cout << "[UBXSecHelper] \t Creating TPC Object " << track_v_v.size() << std::endl;
      //std::cout << "IS NEUTRINO, pfp id " << particle->Self() << std::endl;
      lar_pandora::VertexVector nu_vertex_v;
      auto search = pfParticleToVertexMap.find(particle);
      if(search != pfParticleToVertexMap.end()) {
        nu_vertex_v = search->second;
      }

      double nu_vertex_xyz[3]={0.,0.,0.};
      nu_vertex_v[0]->XYZ(nu_vertex_xyz);
      //if (!this->InFV(nu_vertex_xyz)) continue;

      lar_pandora::TrackVector track_v;
      lar_pandora::PFParticleVector pfp_v;

      CollectTracksAndPFP(pfParticleToTrackMap, pfParticleList, particle, pfp_v, track_v);

      pfp_v_v.emplace_back(pfp_v);
      track_v_v.emplace_back(track_v);

      std::cout << "[UBXSecHelper] \t Number of pfp for this TPC object: "    << pfp_v.size()   << std::endl;
      for (auto pfp : pfp_v) {
        std::cout << "[UBXSecHelper] \t \t PFP " << pfp->Self() << " with pdg " << pfp->PdgCode();
        auto it = pfParticleToVertexMap.find(pfp);
        if (it == pfParticleToVertexMap.end()) {
           std::cout << " and vertex [vertex not available for this PFP]" << std::endl;
        } else {
          double xyz[3];
          (it->second)[0]->XYZ(xyz);
          std::cout << " and vertex " << xyz[0] << " " << xyz[1] << " " << xyz[2] << std::endl;   
        }
      }
      std::cout << std::endl; 
      std::cout << "[UBXSecHelper] \t Number of tracks for this TPC object: " << track_v.size() << std::endl;

      //for (unsigned int i = 0; i < pfp_v.size(); i++) std::cout << "   pfp with ID " << pfp_v[i]->Self() << std::endl;
    } // end if neutrino
  } // end pfp loop

}


//______________________________________________________________________________________
void UBXSecHelper::GetTPCObjects(lar_pandora::PFParticleVector pfParticleList, 
                                 lar_pandora::PFParticlesToShowers pfParticleToShowerMap, 
                                 lar_pandora::PFParticlesToVertices  pfParticleToVertexMap, 
                                 std::vector<lar_pandora::PFParticleVector> & pfp_v_v, 
                                 std::vector<lar_pandora::ShowerVector> & shower_v_v) {

  shower_v_v.clear();
  pfp_v_v.clear();

  std::cout << "[UBXSecHelper] Getting TPC Objects..." << std::endl;

  for (unsigned int n = 0; n < pfParticleList.size(); ++n) {
    const art::Ptr<recob::PFParticle> particle = pfParticleList.at(n);

    if(lar_pandora::LArPandoraHelper::IsNeutrino(particle)) {
      std::cout << "[UBXSecHelper] \t Creating TPC Object " << shower_v_v.size() << std::endl;
      //std::cout << "IS NEUTRINO, pfp id " << particle->Self() << std::endl;
      lar_pandora::VertexVector nu_vertex_v;
      auto search = pfParticleToVertexMap.find(particle);
      if(search != pfParticleToVertexMap.end()) {
        nu_vertex_v = search->second;
      }

      double nu_vertex_xyz[3]={0.,0.,0.};
      nu_vertex_v[0]->XYZ(nu_vertex_xyz);
      //if (!this->InFV(nu_vertex_xyz)) continue;

      lar_pandora::ShowerVector shower_v;
      lar_pandora::PFParticleVector pfp_v;

      CollectShowersAndPFP(pfParticleToShowerMap, pfParticleList, particle, pfp_v, shower_v);

      pfp_v_v.emplace_back(pfp_v);
      shower_v_v.emplace_back(shower_v);

      std::cout << "[UBXSecHelper] \t Number of pfp for this TPC object: "    << pfp_v.size()   << std::endl;
      for (auto pfp : pfp_v) {
        std::cout << "[UBXSecHelper] \t \t PFP " << pfp->Self() << " with pdg " << pfp->PdgCode();
        auto it = pfParticleToVertexMap.find(pfp);
        if (it == pfParticleToVertexMap.end()) {
           std::cout << " and vertex [vertex not available for this PFP]" << std::endl;
        } else {
          double xyz[3];
          (it->second)[0]->XYZ(xyz);
          std::cout << " and vertex " << xyz[0] << " " << xyz[1] << " " << xyz[2] << std::endl;   
        }
      }
      std::cout << std::endl; 
      std::cout << "[UBXSecHelper] \t Number of showers for this TPC object: " << shower_v.size() << std::endl;

      //for (unsigned int i = 0; i < pfp_v.size(); i++) std::cout << "   pfp with ID " << pfp_v[i]->Self() << std::endl;
    } // end if neutrino
  } // end pfp loop

}



//______________________________________________________________________________________________________________________________________
void UBXSecHelper::CollectShowersAndPFP(lar_pandora::PFParticlesToShowers pfParticleToShowerMap,
                                          lar_pandora::PFParticleVector pfParticleList,
                                          art::Ptr<recob::PFParticle> particle,
                                          lar_pandora::PFParticleVector &pfp_v,
                                          lar_pandora::ShowerVector &shower_v) {

  pfp_v.emplace_back(particle);

  lar_pandora::PFParticlesToShowers::const_iterator showerMapIter = pfParticleToShowerMap.find(particle);
  if (showerMapIter != pfParticleToShowerMap.end()) {
    lar_pandora::ShowerVector showers = showerMapIter->second;
    std::cout << "[UBXSecHelper] \t PFP " << particle->Self() << " has " << showers.size() << " showers ass." << std::endl;
    for (unsigned int shwr = 0; shwr < showers.size(); shwr++) {
      shower_v.emplace_back(showers[shwr]);
    }
  }
  const std::vector<size_t> &daughterIDs = particle->Daughters();
  if(daughterIDs.size() == 0) return;
  else {
    for (unsigned int m = 0; m < daughterIDs.size(); ++m) {
      const art::Ptr<recob::PFParticle> daughter = pfParticleList.at(daughterIDs.at(m));
      CollectShowersAndPFP(pfParticleToShowerMap, pfParticleList, daughter, pfp_v, shower_v);
    }
  }

}


//______________________________________________________________________________________________________________________________________
void UBXSecHelper::CollectTracksAndPFP(lar_pandora::PFParticlesToTracks pfParticleToTrackMap,
                                          lar_pandora::PFParticleVector pfParticleList,
                                          art::Ptr<recob::PFParticle> particle,
                                          lar_pandora::PFParticleVector &pfp_v,
                                          lar_pandora::TrackVector &track_v) {

  pfp_v.emplace_back(particle);

  lar_pandora::PFParticlesToTracks::const_iterator trackMapIter = pfParticleToTrackMap.find(particle);
  if (trackMapIter != pfParticleToTrackMap.end()) {
    lar_pandora::TrackVector tracks = trackMapIter->second;
    std::cout << "[UBXSecHelper] \t PFP " << particle->Self() << " has " << tracks.size() << " tracks ass." << std::endl;
    for (unsigned int trk = 0; trk < tracks.size(); trk++) {
      track_v.emplace_back(tracks[trk]);
    }
  }
  const std::vector<size_t> &daughterIDs = particle->Daughters();
  if(daughterIDs.size() == 0) return;
  else {
    for (unsigned int m = 0; m < daughterIDs.size(); ++m) {
      const art::Ptr<recob::PFParticle> daughter = pfParticleList.at(daughterIDs.at(m));
      CollectTracksAndPFP(pfParticleToTrackMap, pfParticleList, daughter, pfp_v, track_v);
    }
  }

}



//______________________________________________________________________________________________________________________________________
bool UBXSecHelper::InFV(double * nu_vertex_xyz){

  double x = nu_vertex_xyz[0];
  double y = nu_vertex_xyz[1];
  double z = nu_vertex_xyz[2];

  //This defines our current settings for the fiducial volume
  double FVx = 256.35;
  double FVy = 233;
  double FVz = 1036.8;
  double borderx = 10.;
  double bordery = 20.;
  double borderz = 10.;
  //double cryoradius = 191.61;
  //double cryoz = 1086.49 + 2*67.63;

  if(x < (FVx - borderx) && (x > borderx) && (y < (FVy/2. - bordery)) && (y > (-FVy/2. + bordery)) && (z < (FVz - borderz)) && (z > borderz)) return true;
  return false;

}



//__________________________________________________________________________
ubana::TPCObjectOrigin UBXSecHelper::GetSliceOrigin(std::vector<art::Ptr<recob::PFParticle>> neutrinoOriginPFP, std::vector<art::Ptr<recob::PFParticle>> cosmicOriginPFP, lar_pandora::PFParticleVector pfp_v) {

  ::ubana::TPCObjectOrigin origin = ubana::kUnknown;

  int nuOrigin     = 0;
  int cosmicOrigin = 0;

  // Loop over pfp in the slice
  for ( unsigned int i = 0; i < pfp_v.size(); i++) {

    // Loop over pfp from nu origin
    for ( unsigned int j = 0; j < neutrinoOriginPFP.size(); j++) {

      if (neutrinoOriginPFP[j] == pfp_v[i]) {
        nuOrigin ++;
      }
    }

    // Loop over pfp from cosmic origin
    for ( unsigned int j = 0; j < cosmicOriginPFP.size(); j++) {

      if (cosmicOriginPFP[j] == pfp_v[i]) {
        cosmicOrigin ++;
      }
    }
  }

  if (nuOrigin > 0  && cosmicOrigin == 0) origin = ubana::kBeamNeutrino;
  if (nuOrigin == 0 && cosmicOrigin > 0 ) origin = ubana::kCosmicRay;
  if (nuOrigin > 0  && cosmicOrigin > 0 ) origin = ubana::kMixed;
  
  return origin;

}

//______________________________________________________________________________
bool UBXSecHelper::TrackPassesHitRequirment(art::Event const & e,
                                            std::string _particleLabel,
                                            art::Ptr<recob::Track> trk,
                                            int nHitsReq) {

  lar_pandora::TrackVector trackVector;
  lar_pandora::TracksToHits tracksToHits;
  lar_pandora::LArPandoraHelper::CollectTracks( e, _particleLabel, trackVector, tracksToHits );

  lar_pandora::HitVector hit_v = tracksToHits.at(trk);

  int nhits_u = 0;
  int nhits_v = 0;
  int nhits_w = 0;

  // Check where the hit is coming from
  for (unsigned int h = 0; h < hit_v.size(); h++){

    if (hit_v[h]->View() == 0) nhits_u++;
    if (hit_v[h]->View() == 1) nhits_v++;
    if (hit_v[h]->View() == 2) nhits_w++;

  }

  if ( (nhits_u > nHitsReq) || (nhits_v > nHitsReq) || (nhits_w > nHitsReq) )
    return true;
  else
    return false;

}

//______________________________________________________________________________

void UBXSecHelper::GetNumberOfHitsPerPlane(art::Event const & e,
                                              std::string _particleLabel,
                                              lar_pandora::ShowerVector shower_v,
                                              int & nhits_u, 
                                              int & nhits_v, 
                                              int & nhits_w ) {
 
  nhits_u = 0;
  nhits_v = 0;
  nhits_w = 0;

  lar_pandora::ShowerVector showerVector;
  lar_pandora::ShowersToHits showersToHits;
  lar_pandora::LArPandoraHelper::CollectShowers( e, _particleLabel, showerVector, showersToHits );

  // Loop over the tracks in this TPC Object
  for (unsigned int t = 0; t < shower_v.size(); t++) {

    // Get the hits associated with the track
    lar_pandora::HitVector hit_v = showersToHits.at(shower_v[t]);

    // Check where the hit is coming from
    for (unsigned int h = 0; h < hit_v.size(); h++){

      if (hit_v[h]->View() == 0) nhits_u++;
      if (hit_v[h]->View() == 1) nhits_v++;
      if (hit_v[h]->View() == 2) nhits_w++;

    }
  }

}

//______________________________________________________________________________

void UBXSecHelper::GetNumberOfHitsPerPlane(art::Event const & e,
                                              std::string _particleLabel,
                                              lar_pandora::TrackVector track_v,
                                              int & nhits_u, 
                                              int & nhits_v, 
                                              int & nhits_w ) {
 
  nhits_u = 0;
  nhits_v = 0;
  nhits_w = 0;

  lar_pandora::TrackVector trackVector;
  lar_pandora::TracksToHits tracksToHits;
  lar_pandora::LArPandoraHelper::CollectTracks( e, _particleLabel, trackVector, tracksToHits );

  // Loop over the tracks in this TPC Object
  for (unsigned int t = 0; t < track_v.size(); t++) {

    // Get the hits associated with the track
    lar_pandora::HitVector hit_v = tracksToHits.at(track_v[t]);

    // Check where the hit is coming from
    for (unsigned int h = 0; h < hit_v.size(); h++){

      if (hit_v[h]->View() == 0) nhits_u++;
      if (hit_v[h]->View() == 1) nhits_v++;
      if (hit_v[h]->View() == 2) nhits_w++;

    }
  }

}

//_________________________________________________________________________________
bool UBXSecHelper::IsCrossingTopBoundary(recob::Track track, int & vtx_ok){

  double vtx[3];
  vtx[0] = track.Vertex().X();
  vtx[1] = track.Vertex().Y();
  vtx[2] = track.Vertex().Z();
  std::cout << "VTX X " <<vtx[0] << "Y " <<vtx[1] << "Z " <<vtx[2] << std::endl;

  double end[3];
  end[0] = track.End().X();
  end[1] = track.End().Y();
  end[2] = track.End().Z();
  std::cout << "END X " <<end[0] << "Y " <<end[1] << "Z " <<end[2] << std::endl;

  if ( InFV(vtx) && !InFV(end) && (end[1] > 233/2.-20)) {
    std::cout << "Crossing top boundary, vertex is in FV" << std::endl;
    vtx_ok = 0;
    return true;
  } else if (!InFV(vtx) && InFV(end) && (vtx[1] > 233/2.-20))  {
    std::cout << "Crossing top boundary, end is in FV" << std::endl;
    vtx_ok = 1;
    return true;
  }
  else {
    vtx_ok = -1;
    return false;
  }

}

//_________________________________________________________________________________
bool UBXSecHelper::IsCrossingBoundary(recob::Track track, int & vtx_ok){

  double vtx[3];
  vtx[0] = track.Vertex().X();
  vtx[1] = track.Vertex().Y();
  vtx[2] = track.Vertex().Z();

  double end[3];
  end[0] = track.End().X();
  end[1] = track.End().Y();
  end[2] = track.End().Z();

  if ( InFV(vtx) && !InFV(end) ) {
    vtx_ok = 0;
    return true;
  } else if (!InFV(vtx) && InFV(end))  {
    vtx_ok = 1;
    return true;
  }
  else {
    vtx_ok = -1;
    return false;
  }

}



//_________________________________________________________________________________
bool UBXSecHelper::GetLongestTrackFromTPCObj(lar_pandora::TrackVector track_v, recob::Track & out_track) {

  if (track_v.size() == 0) {
    return false;
  }

  int length = -1;
  int longest_track = -1;
  for (unsigned int t = 0; t < track_v.size(); t++){

    if (track_v[t]->Length() > length){

      length = track_v[t]->Length();
      longest_track = t;

    }

  }

  if (longest_track > -1){
    out_track = (*track_v[longest_track]);
    return true;
  } else {
    return false;
  }


}




//_________________________________________________________________________________
bool UBXSecHelper::PointIsCloseToDeadRegion(double *reco_nu_vtx, int plane_no){

  ::art::ServiceHandle<geo::Geometry> geo;

  // Get nearest channel
  raw::ChannelID_t ch = geo->NearestChannel(reco_nu_vtx, plane_no);
  //std::cout << "nearest channel is " << ch << std::endl;

  // Get channel status
  const lariov::ChannelStatusProvider& chanFilt = art::ServiceHandle<lariov::ChannelStatusService>()->GetProvider();
  //std::cout << "ch status " << chanFilt.Status(ch) << std::endl;
  if( chanFilt.Status(ch) < 4) return true;

  // Now check close wires
  for(unsigned int new_ch = ch - 5; new_ch < ch - 5 + 11; new_ch++){
    //std::cout << "now trying with channel " << new_ch << std::endl;
    if( new_ch < 0 || new_ch >= 8256) continue;
    if( chanFilt.Status(new_ch) < 4 ) return true;
  }

/*
  double new_point_in_tpc[3];
  for (int z_off = -5; z_off < 5; z_off += 2.5) {
    new_point_in_tpc[0] = reco_nu_vtx[0];
    new_point_in_tpc[1] = reco_nu_vtx[1];
    new_point_in_tpc[2] = reco_nu_vtx[2] + z_off;
    //std::cout << "trying with point " << new_point_in_tpc[0] << " " << new_point_in_tpc[1] << " " << new_point_in_tpc[2] << std::endl;

    raw::ChannelID_t ch = geo->NearestChannel(new_point_in_tpc, 2);
    if( chanFilt.Status(ch) < 4) return true;
  }
*/

  return false;

}


//______________________________________________________________________________
int UBXSecHelper::GetClosestPMT(double *charge_center) {

  int pmt_id= -1;

  ::art::ServiceHandle<geo::Geometry> geo;
  double xyz[3];
  double dist;
  double min_dist = 1.e9;

  for (size_t opch = 0; opch < 32; opch++) {
    geo->OpDetGeoFromOpChannel(opch).GetCenter(xyz); 
    dist = std::sqrt( (charge_center[0] - xyz[0])*(charge_center[0] - xyz[0]) +
                      (charge_center[1] - xyz[1])*(charge_center[1] - xyz[1]) +
                      (charge_center[2] - xyz[2])*(charge_center[2] - xyz[2]) );

    if (dist < min_dist) {
      min_dist = dist;
      pmt_id = opch;
    }        
  }

  return pmt_id;
}


//_______________________________________________________________________________
double UBXSecHelper::GetFlashZCenter(std::vector<double> hypo_pe) {

  double Zcenter = 0.;
  double totalPE = 0.;
  double sumz = 0.;

  ::art::ServiceHandle<geo::Geometry> geo;

  // opdet=>opchannel mapping
  std::vector<size_t> opdet2opch(geo->NOpDets(),0);
  for(size_t opch=0; opch<opdet2opch.size(); ++opch){
    opdet2opch[geo->OpDetFromOpChannel(opch)] = opch;
  }

  for (unsigned int opdet = 0; opdet < hypo_pe.size(); opdet++) {

    size_t opch = opdet2opch[opdet];

    // Get physical detector location for this opChannel
    double PMTxyz[3];
    geo->OpDetGeoFromOpChannel(opch).GetCenter(PMTxyz);

    // Add up the position, weighting with PEs
    sumz    += hypo_pe[opdet]*PMTxyz[2];

    totalPE += hypo_pe[opdet];
  }

  Zcenter = sumz/totalPE;

  return Zcenter;

}



