// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/AbstractChainRecorder.h>
#include <volition/Block.h>
#include <volition/Digest.h>
#include <volition/HTTPMiningMessenger.h>
#include <volition/Miner.h>
#include <volition/MinerLaunchTests.h>

namespace Volition {

//================================================================//
// Miner
//================================================================//

//----------------------------------------------------------------//
void Miner::affirmBranchSearch ( BlockTreeNode::ConstPtr node ) {

    while ( node && node->checkStatus (( BlockTreeNode::Status )( BlockTreeNode::STATUS_NEW | BlockTreeNode::STATUS_MISSING ))) {
        this->affirmNodeSearch ( node );
        node = node->getParent ();
    }
}

//----------------------------------------------------------------//
void Miner::affirmNodeSearch ( BlockTreeNode::ConstPtr node ) {

    if ( node->checkStatus (( BlockTreeNode::Status )( BlockTreeNode::STATUS_COMPLETE | BlockTreeNode::STATUS_INVALID ))) return;

    string hash = ( **node ).getDigest ();
    
    if ( this->mSearches.find ( hash ) != this->mSearches.end ()) return; // already searching

    MinerSearchEntry& search = this->mSearches [ hash ];
    
    search.mSearchTarget = node;
    search.mSearchLimit = 0;
    
    map < string, RemoteMiner >::iterator remoteMinerIt = this->mRemoteMiners.begin ();
    for ( ; remoteMinerIt != this->mRemoteMiners.end (); ++remoteMinerIt ) {
        
        RemoteMiner& remoteMiner = remoteMinerIt->second;
        
        this->mMessenger->requestBlock ( *this, remoteMinerIt->first, remoteMiner.mURL, ( **search.mSearchTarget ).getDigest ());
        search.mSearchLimit++;
    }
}

//----------------------------------------------------------------//
bool Miner::canExtend () const {

    if ( !( this->mBestBranch && this->mBestBranch->checkStatus ( BlockTreeNode::STATUS_COMPLETE ))) return false;

    size_t count = 0;
    
    map < string, RemoteMiner >::const_iterator minerIt = this->mRemoteMiners.cbegin ();
    for ( ; minerIt != this->mRemoteMiners.cend (); ++minerIt ) {
        const RemoteMiner& remoteMiner = minerIt->second;
        if ( remoteMiner.mTag && this->mBestBranch->isAncestorOf ( remoteMiner.mTag )) count++;
    }
    return ( count > ( this->mRemoteMiners.size () >> 1 ));
}

//----------------------------------------------------------------//
void Miner::composeChain () {

    if ( this->mChainTag == this->mBestBranch ) return;

    if ( this->mBestBranch->isAncestorOf ( this->mChainTag )) {
        this->mChain->reset (( **this->mBestBranch ).getHeight () + 1 );
        this->mChainTag = this->mBestBranch;
        return;
    }

    // if chain is divergent from best branch, re-root it
    if ( !this->mChainTag->isAncestorOf ( this->mBestBranch )) {
                    
        // REWIND chain to point of divergence
        BlockTreeNode::ConstPtr root = BlockTreeNode::findRoot ( this->mChainTag, this->mBestBranch ).mRoot;
        assert ( root ); // guaranteed -> common genesis
        assert ( root->checkStatus ( BlockTreeNode::STATUS_COMPLETE ));  // guaranteed -> was in chain
        
        this->mChain->reset (( **root ).getHeight () + 1 );
        this->mChainTag = root;
    }
    assert ( this->mChainTag->isAncestorOf ( this->mBestBranch ));
    
    this->updateChainRecurse ( this->mBestBranch );
}

//----------------------------------------------------------------//
Miner::Miner () {
}

//----------------------------------------------------------------//
Miner::~Miner () {
}

//----------------------------------------------------------------//
void Miner::processResponses () {

    if ( this->mMinerID == "9090" ) {
        printf ( "" );
    }

    for ( ; this->mBlockQueue.size (); this->mBlockQueue.pop_front ()) {
    
        const BlockQueueEntry& entry = *this->mBlockQueue.front ().get ();
        
        switch ( entry.mRequest.mRequestType ) {
        
            case MiningMessengerRequest::REQUEST_HEADER: {
                
                string minerID = entry.mRequest.mMinerID;
                RemoteMiner& remoteMiner = this->mRemoteMiners [ minerID ];
        
                if ( entry.mBlockHeader ) {
                
                    remoteMiner.mTag = entry.mBlockHeader ? this->mBlockTree.affirmBlock ( entry.mBlockHeader, NULL ) : NULL;

                    if ( remoteMiner.mTag ) {

                        // header found a parent; advance to the next header;
                        remoteMiner.mCurrentBlock = entry.mBlockHeader->getHeight () + 1;

                        // next block may already be in the cache, since we may have filled the cache by backing up.
                        map < size_t, shared_ptr < const BlockHeader >>::iterator cacheIt = remoteMiner.mHeaderCache.begin ();
                        while ( cacheIt != remoteMiner.mHeaderCache.end ()) {

                            shared_ptr < const BlockHeader > header = ( cacheIt++ )->second;
                            size_t height = header->getHeight ();

                            // no more current or previous headers in the cache; bail.
                            if ( height > remoteMiner.mCurrentBlock ) break;

                            // cache has the headers we need; try to add it.
                            if ( height == remoteMiner.mCurrentBlock ) {

                                BlockTreeNode::ConstPtr node = this->mBlockTree.affirmBlock ( header, NULL );
                                if ( node ) {

                                    // header found a root and was added, so advance to the next header;
                                    remoteMiner.mTag = node;
                                    remoteMiner.mCurrentBlock = height + 1;
                                }
                                else {

                                    // cache may be bad; empty it and bail;
                                    remoteMiner.mHeaderCache.clear ();
                                    break;
                                }
                            }

                            // header is no longer needed.
                            remoteMiner.mHeaderCache.erase ( height );
                        }
                    }
                    else {
                    
                        // header is dangling; cache it and request the previous header.
                        size_t height = entry.mBlockHeader->getHeight ();
                        remoteMiner.mHeaderCache [ height ] = entry.mBlockHeader;
                        remoteMiner.mCurrentBlock = height - 1;
                    }
                }

                if ( this->mMinerSet.find ( minerID ) != this->mMinerSet.end ()) {
                    this->mMinerSet.erase ( minerID );
                }
                break;
            }
            
            case MiningMessengerRequest::REQUEST_BLOCK: {
                
                this->mBlockTree.update ( entry.mBlock ); // update no matter what (will do nothing if node is missing)
                
                string hash = entry.mRequest.mBlockDigest;
                map < string, MinerSearchEntry >::iterator searchIt = this->mSearches.find ( hash );
                if ( searchIt == this->mSearches.end ()) break;
                
                MinerSearchEntry& search = searchIt->second;
                search.mSearchCount++;

                if (( search.mSearchCount >= search.mSearchLimit ) && search.mSearchTarget->checkStatus ( BlockTreeNode::STATUS_NEW )) {
                    this->mBlockTree.mark ( search.mSearchTarget, BlockTreeNode::STATUS_MISSING );
                }

                if ( !search.mSearchTarget->checkStatus ( BlockTreeNode::STATUS_NEW )) {
                    this->mSearches.erase ( hash );
                }
                break;
            }
            
            default:
                assert ( false );
                break;
        }
    }
}

//----------------------------------------------------------------//
void Miner::requestHeaders () {

    this->affirmMessenger ();
    
    map < string, RemoteMiner >::iterator remoteMinerIt = this->mRemoteMiners.begin ();
    for ( ; remoteMinerIt != this->mRemoteMiners.end (); ++remoteMinerIt ) {
    
        RemoteMiner& remoteMiner = remoteMinerIt->second;
        
        // constantly refill active set
        if ( this->mMinerSet.find ( remoteMinerIt->first ) == this->mMinerSet.end ()) {
            this->mMessenger->requestHeader ( *this, remoteMinerIt->first, remoteMiner.mURL, remoteMiner.mCurrentBlock );
            this->mMinerSet.insert ( remoteMinerIt->first );
        }
    }
}

//----------------------------------------------------------------//
void Miner::selectBestBranch ( time_t now ) {

    if ( this->mMinerID == "9090" ) {
        printf ( "" );
    }

    map < string, RemoteMiner >::const_iterator remoteMinerIt = this->mRemoteMiners.begin ();
    for ( ; remoteMinerIt != this->mRemoteMiners.end (); ++remoteMinerIt ) {
        const RemoteMiner& remoteMiner = remoteMinerIt->second;

        if ( !remoteMiner.mTag ) continue;

        BlockTreeNode::ConstPtr truncated = this->truncate (
            remoteMiner.mTag->trim (( BlockTreeNode::Status )( BlockTreeNode::STATUS_MISSING )),
            now
        );
        
        if ( BlockTreeNode::compare ( truncated, this->mBestBranch, this->mRewriteMode, this->mRewriteWindowInSeconds ) < 0 ) {
            this->mBestBranch = truncated;
        }
    }
    assert ( this->mBestBranch );
}

//----------------------------------------------------------------//
void Miner::step ( time_t now ) {

    Poco::ScopedLock < Poco::Mutex > scopedLock ( this->mMutex );

    this->processTransactions ( *this );
    
    if ( this->mMessenger ) {
    
        // APPLY incoming blocks
        this->processResponses ();
        
        // CHOOSE new branch
        this->selectBestBranch ( now );
        
        // SEARCH for missing blocks
        this->updateSearches ( now );
        
        // BUILD the current chain
        this->composeChain ();
        
        // EXTEND chain if complete and has consensus
        if ( this->canExtend ()) {
            this->extend ( now );
        }

        // SCAN the ledger for miners
        this->discoverMiners ();
        
        // QUERY the network for headers
        this->requestHeaders ();
    }
    else {
        this->extend ( now );
    }
}

//----------------------------------------------------------------//
BlockTreeNode::ConstPtr Miner::truncate ( BlockTreeNode::ConstPtr tail, time_t now ) const {

    if ( this->mRewriteMode == BlockTreeNode::REWRITE_NONE ) return tail;

    // if a block from self would be more charming at any point along the chain,
    // truncate the chain to the parent of that block. in other words: seek the
    // earliest insertion point for a local block. if we find a block from
    // self, abort: to truncate, our local block must *beat* any other block.

    // TODO: this should take into account the lookback window.

    BlockTreeNode::ConstPtr cursor = tail;
    
    while ( cursor ) {
    
        const BlockHeader& header = **cursor;
    
        if (( this->mRewriteMode == BlockTreeNode::REWRITE_WINDOW ) && !header.isInRewriteWindow ( this->mRewriteWindowInSeconds, now )) return tail;
    
        BlockTreeNode::ConstPtr parent = cursor->getParent ();
        if ( !parent ) break;
        
        const BlockHeader& parentHeader = **parent;
        
        if ( header.getMinerID () == this->mMinerID ) break;
        
        Digest charm = parentHeader.getNextCharm ( this->mVisage );
        if ( BlockHeader::compare ( charm, header.getCharm ()) < 0 ) return parent;
        
        cursor = parent;
    }

    return tail;
}

//----------------------------------------------------------------//
void Miner::updateChainRecurse ( BlockTreeNode::ConstPtr branch ) {

    if ( this->mChainTag == branch ) return; // nothing to do
    
    BlockTreeNode::ConstPtr parent = branch->getParent ();
    if ( parent != this->mChainTag ) {
        this->updateChainRecurse ( parent );
    }
    
    if ( branch->checkStatus ( BlockTreeNode::STATUS_COMPLETE )) {
        this->pushBlock ( branch->getBlock ());
        assert ( this->mChainTag == branch );
    }
}

//----------------------------------------------------------------//
void Miner::updateSearches ( time_t now ) {

    map < string, RemoteMiner >::const_iterator remoteMinerIt = this->mRemoteMiners.begin ();
    for ( ; remoteMinerIt != this->mRemoteMiners.end (); ++remoteMinerIt ) {
        
        const RemoteMiner& remoteMiner = remoteMinerIt->second;

        // we only care about missing branches; ignore new/complete/invalid branches.
        if ( remoteMiner.mTag && remoteMiner.mTag->checkStatus ( BlockTreeNode::STATUS_MISSING )) {
        
            BlockTreeNode::ConstPtr truncated = this->truncate ( remoteMiner.mTag, now );
        
            // only affirm a search if the other chain could beat our current.
            if ( BlockTreeNode::compare ( truncated, this->mBestBranch, this->mRewriteMode, this->mRewriteWindowInSeconds ) < 0 ) {
                this->affirmBranchSearch ( truncated );
            }
        }
    }
    // always affirm a search for the current branch
    this->affirmBranchSearch ( this->mBestBranch );
}

} // namespace Volition
