// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/Release.h>
#include <volition/simulation/SimMiner.h>
#include <volition/UnsecureRandom.h>

namespace Volition {
namespace Simulation {

//================================================================//
// SimMiner
//================================================================//

//----------------------------------------------------------------//
void SimMiner::extendChain ( string charmHex, time_t time ) {

    shared_ptr < const Block > prevBlock = this->mLedger->getBlock ();

    shared_ptr < Block > block = make_shared < Block >();
    block->initialize (
        this->mMinerID,
        VOL_NODE_RELEASE,
        this->mVisage,
        time,
        prevBlock ? prevBlock.get () : NULL,
        this->mKeyPair
    );
    
    Digest charm = block->getCharm ();
    std::fill ( charm.begin (), charm.end (), 0 );
    string compose = charm.toHex ();
    compose.replace ( 0, charmHex.size (), charmHex );
    charm.fromHex ( compose );
    
    block->setCharm ( charm );
    block->sign ( this->mKeyPair, Digest::DEFAULT_HASH_ALGORITHM );
    
    this->pushBlock ( block );
}

//----------------------------------------------------------------//
shared_ptr < Block > SimMiner::replaceBlock ( shared_ptr < const Block > oldBlock, string charmHex ) {
    
    assert ( oldBlock );
    
    BlockTreeCursor prevNode = this->mBlockTree->findCursorForHash ( oldBlock->getPrevDigest ());
    assert ( prevNode.hasHeader ());
    
    shared_ptr < const Block > prevBlock = prevNode.getBlock ();
    assert ( prevBlock );
    
    shared_ptr < Block > block = make_shared < Block >();
    block->initialize (
        this->mMinerID,
        VOL_NODE_RELEASE,
        this->mVisage,
        oldBlock->getTime (),
        prevBlock.get (),
        this->mKeyPair
    );
    
    Digest charm = block->getCharm ();
    std::fill ( charm.begin (), charm.end (), 0 );
    string compose = charm.toHex ();
    compose.replace ( 0, charmHex.size (), charmHex );
    charm.fromHex ( compose );
    
    block->setCharm ( charm );
    block->sign ( this->mKeyPair, Digest::DEFAULT_HASH_ALGORITHM );
    return block;
}

//----------------------------------------------------------------//
void SimMiner::rewindChain ( size_t height ) {

    BlockTreeCursor bestCursor = this->mBestBranchTag.getCursor ();
    if ( !bestCursor.hasHeader ()) return;

    while ( bestCursor.getHeight () > height ) {
        bestCursor = bestCursor.getParent ();
        this->mBlockTree->tag ( this->mBestBranchTag, bestCursor );
    }
    this->composeChain ( bestCursor );
}

//----------------------------------------------------------------//
void SimMiner::setActive ( bool active ) {

    this->mActive = active;
}

//----------------------------------------------------------------//
void SimMiner::setCharm ( size_t height, string charmHex ) {

    BlockTreeCursor cursor = *this->mBestBranchTag;
    while ( cursor.hasHeader ()) {
        size_t cursorHeight = cursor.getHeight ();
        if ( cursorHeight == height ) {

            shared_ptr < Block > block = this->replaceBlock ( cursor.getBlock (), charmHex );
            this->mBlockTree->affirmBlock ( this->mBestBranchTag, block );
            this->composeChain ( this->mBestBranchTag.getCursor ());
            return;
        }
        assert ( cursorHeight > height );
        cursor = cursor.getParent ();
    }
}

//----------------------------------------------------------------//
void SimMiner::scrambleRemotes () {

//    set < shared_ptr < RemoteMiner >>::iterator remoteMinerIt = this->mOnlineMiners.begin ();
//    for ( ; remoteMinerIt != this->mOnlineMiners.end (); ++remoteMinerIt ) {
//        shared_ptr < RemoteMiner > remoteMiner = *remoteMinerIt;
//                
//        size_t height = remoteMiner->mTag.getHeight ();
//        height = ( size_t )floor ( height * UnsecureRandom::get ().random ());
//        
//        BlockTreeCursor cursor = *remoteMiner->mTag;
//        while ( cursor.getHeight () > height ) {
//            cursor = cursor.getParent ();
//        }
//        
//        this->mBlockTree->tag ( remoteMiner->mTag, cursor );
//        remoteMiner->mHeight = height + 1;
//        remoteMiner->mForward = true;
//    }
}

//----------------------------------------------------------------//
SimMiner::SimMiner ( string url, bool isGenesisMiner ) :
    mURL ( url ),
    mActive ( true ),
    mInterval ( 1 ),
    mIsGenesisMiner ( isGenesisMiner ) {

    this->mBlockVerificationPolicy = Block::VerificationPolicy::NONE;
}

//----------------------------------------------------------------//
SimMiner::~SimMiner () {
}

} // namespace Simulation
} // namespace Volition
