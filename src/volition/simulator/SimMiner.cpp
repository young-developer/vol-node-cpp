// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/TheContext.h>
#include <volition/simulator/SimMiner.h>
#include <volition/simulator/TheSimulator.h>
#include <volition/Transactions.h>

namespace Volition {
namespace Simulator {

//================================================================//
// SimMiner
//================================================================//

//----------------------------------------------------------------//
const SimMiner* SimMiner::nextMiner () {

    if ( this->mMinerCursor < this->mMinerQueue.size ()) {
    
        int minerIdx = this->mMinerQueue [ this->mMinerCursor++ ];
        const SimMiner* miner = &TheSimulator::get ().getMiner ( minerIdx );
        return ( this == miner ) ? this->nextMiner () : miner;
    }
    return 0;
}

//----------------------------------------------------------------//
void SimMiner::pushGenesisTransaction ( Block& block ) const {

    unique_ptr < Transaction::GenesisMiner > genesisMinerTransaction = make_unique < Transaction::GenesisMiner >();
    
    genesisMinerTransaction->mAccountName = this->mMinerID;
    genesisMinerTransaction->mKey = this->mKeyPair;
    genesisMinerTransaction->mKeyName = "master";
    genesisMinerTransaction->mAmount = 0;
    genesisMinerTransaction->mURL = "";

    block.pushTransaction ( move ( genesisMinerTransaction ));

}

//----------------------------------------------------------------//
void SimMiner::print () const {

    //printf ( "[%s] ", this->mCohort ? this->mCohort->mName.c_str () : "" );
    this->mChain->print ();
}

//----------------------------------------------------------------//
void SimMiner::resetMinerQueue () {

    TheSimulator::get ().resetMinerQueue ( this->mMinerQueue, true );
    this->mMinerCursor = 0;
}

//----------------------------------------------------------------//
void SimMiner::step () {

    if ( this->mMinerCursor >= this->mMinerQueue.size ()) {
        this->resetMinerQueue ();
        this->pushBlock ( *this->mChain, true );
    }
    
    const SimMiner* miner = this->nextMiner ();
    if ( !miner ) return;

    unique_ptr < Chain > chain = make_unique < Chain >( *miner->mChain );

    if ( this->mVerbose ) {
        printf ( " player: %s\n", this->mMinerID.c_str ());
        this->mChain->print ( "   CHAIN0: " );
        chain->print ( "   CHAIN1: " );
    }

    this->updateChain ( move ( chain ));

    if ( this->mVerbose ) {
        this->mChain->print ( "     BEST: " );
        printf ( "\n" );
    }
}

//----------------------------------------------------------------//
SimMiner::SimMiner () :
    mCohort ( 0 ),
    mFrequency ( 1 ),
    mVerbose ( false ),
    mMinerCursor ( 0 ) {
    
    this->mKeyPair.elliptic ( CryptoKey::DEFAULT_EC_GROUP_NAME );
}

//----------------------------------------------------------------//
SimMiner::~SimMiner () {
}

} // namespace Simulator
} // namespace Volition
