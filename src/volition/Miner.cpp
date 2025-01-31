// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/Block.h>
#include <volition/BlockODBM.h>
#include <volition/BlockSearchPool.h>
#include <volition/Digest.h>
#include <volition/FileSys.h>
#include <volition/HTTPMiningMessenger.h>
#include <volition/InMemoryBlockTree.h>
#include <volition/Miner.h>
#include <volition/MinerLaunchTests.h>
#include <volition/Release.h>
#include <volition/SQLiteBlockTree.h>
#include <volition/Transaction.h>
#include <volition/Transactions.h>
#include <volition/UnsecureRandom.h>

namespace Volition {

//================================================================//
// MinerSnapshot
//================================================================//

//----------------------------------------------------------------//
MinerSnapshot::MinerSnapshot () :
    mIsMiner ( false ) {
}

//----------------------------------------------------------------//
set < string > MinerSnapshot::sampleOnlineMinerURLs ( size_t sampleSize ) const {
    
    return UnsecureRandom::get ().sampleSet < string > ( this->mOnlineMinerURLs, sampleSize );
}

//================================================================//
// Miner
//================================================================//

//----------------------------------------------------------------//
void Miner::affirmKey ( uint keyLength, unsigned long exp ) {

    if ( !this->mKeyPair ) {
        CryptoKeyPair keyPair;
        keyPair.rsa ( keyLength, exp );
        this->setKeyPair ( keyPair );
    }
    assert ( this->mKeyPair );
}

//----------------------------------------------------------------//
void Miner::affirmMessenger () {

    if ( !this->mMessenger ) {
        this->mMessenger = make_shared < HTTPMiningMessenger >();
    }
}

//----------------------------------------------------------------//
void Miner::affirmRemoteMiner ( string url ) {

    // TODO: properly check and formal URLs
    while ( url.size () && ( url [ url.size () - 1 ] == '/' )) {
        url.pop_back ();
    }

    if ( url.size () && ( this->mCompletedURLs.find ( url ) == this->mCompletedURLs.cend ())) {
        this->mNewMinerURLs.insert ( url );
    }
}

//----------------------------------------------------------------//
void Miner::affirmVisage () {

    this->mVisage = Miner::calculateVisage ( this->mKeyPair, this->mMotto );
}

//----------------------------------------------------------------//
Signature Miner::calculateVisage ( string motto ) {

    return Miner::calculateVisage ( this->mKeyPair, motto );
}

//----------------------------------------------------------------//
Signature Miner::calculateVisage ( const CryptoKeyPair& keyPair, string motto ) {

    assert ( keyPair );
    return keyPair.sign ( motto, Digest::HASH_ALGORITHM_SHA256 );
}

//----------------------------------------------------------------//
bool Miner::checkTags () const {

    BlockTreeCursor ledgerCursor = *this->mLedgerTag;
    assert ( this->mLedger->countBlocks () == ( ledgerCursor.getHeight () + 1 ));
    shared_ptr < const Block > ledgerBlock = this->mLedger->getBlock ();
    assert ( ledgerBlock );
    return ledgerBlock->equals ( ledgerCursor );
}

//----------------------------------------------------------------//
void Miner::composeChain ( BlockTreeCursor cursor ) {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    if ( this->mLedgerTag.getCursor ().equals ( cursor )) return;

    BlockTreeCursor ledgerCursor = this->mLedgerTag.getCursor ();

    // check to see if cursor is *behind* best branch
    if ( cursor.isAncestorOf ( ledgerCursor )) {
        this->mLedger->revertAndClear ( cursor.getHeight () + 1 );
        this->mBlockTree->tag ( this->mLedgerTag, cursor);
        return;
    }

    // if chain is divergent from best branch, re-root it
    if ( !ledgerCursor.isAncestorOf ( cursor )) {
        
        // REWIND chain to point of divergence
        BlockTreeCursor root = BlockTreeCursor::findRoot ( ledgerCursor, cursor );
        assert ( root.hasHeader ()); // guaranteed -> common genesis
        assert ( root.isComplete ());  // guaranteed -> was in chain
        
        this->mLedger->revertAndClear ( root.getHeight () + 1 );
        this->mBlockTree->tag ( this->mLedgerTag, root );
        ledgerCursor = this->mLedgerTag.getCursor ();
    }
    
    assert ( ledgerCursor.isAncestorOf ( cursor ));
    
    this->composeChainInnerLoop ( cursor );
    
    assert ( this->checkTags ());
}

//----------------------------------------------------------------//
void Miner::composeChainInnerLoop ( BlockTreeCursor branch ) {

    // we're descending to find this cursor
    BlockTreeCursor ledgerCursor = this->mLedgerTag.getCursor ();

    list < BlockTreeCursor > stack;

    {
        LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, "Building block stack." );
        
        while ( !ledgerCursor.equals ( branch )) {
            
            // TODO: this is gross. it's a cleanup for broken branch status. need to make sure branch status can't break.
            // TODO: find how how the branch could get into the broken state (i.e. parent is complete, but child is not.
            if (( branch.getHeight () == ( ledgerCursor.getHeight () + 1 )) && ( branch.hasBlock () && !branch.isComplete ())) {
                this->mBlockTree->setBranchStatus ( branch, BRANCH_STATUS_COMPLETE );
                return;
            }
            
            if ( branch.isComplete ()) {
                stack.push_front ( branch );
            }
            branch = branch.getParent ();
        }
    }

    {
        LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, "Applying block stack." );
    
        LGN_LOG ( VOL_FILTER_CONSENSUS, INFO, "Block stack height: %d", ( int )stack.size ());
    
        list < BlockTreeCursor >::iterator stackIt = stack.begin ();
        for ( size_t i = 0; stackIt != stack.end (); ++stackIt, ++i ) {
        
            BlockTreeCursor& cursor = *stackIt;
        
            LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, "Block %d: %d of %d", ( int )cursor.getHeight (), ( int )i, ( int )stack.size ());
            
            this->pushBlock ( cursor.getBlock ());
            assert (( *this->mLedgerTag ).equals ( *stackIt ));
            
            if ( this->mPersistFrequency && (( i % this->mPersistFrequency ) == 0 )) {
                this->saveChain ();
            }
        }
    }
}

//----------------------------------------------------------------//
size_t Miner::getChainSize () const {

    return this->mLedger->countBlocks ();
}

//----------------------------------------------------------------//
Ledger& Miner::getLedger () {

    assert ( this->mLedger );
    return *this->mLedger;
}

//----------------------------------------------------------------//
Ledger Miner::getLedgerAtBlock ( u64 index ) const {

    Ledger ledger ( *this->mLedger );
    ledger.revert ( index );
    return ledger;
}

//----------------------------------------------------------------//
void Miner::getSnapshot ( MinerSnapshot* snapshot, MinerStatus* status ) {

    if ( !( snapshot || status )) return;

    this->mSnapshotMutex.lock_shared ();
    
    if ( snapshot ) {
        *snapshot = this->mSnapshot;
    }
    
    if ( status ) {
        *status = this->mMinerStatus;
    }
    
    this->mSnapshotMutex.unlock_shared ();
}

//----------------------------------------------------------------//
BlockTreeCursor Miner::improveBranch ( BlockTreeCursor tail, u64 consensusHeight, time_t now ) {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    // if muted, no change is possible
    if ( this->mFlags & MINER_MUTE ) return tail;

    // first, see if we're authorized to mine. if we're not, then we can't change the branch, so return.
    if ( !this->mIsMiner ) return tail;

    // height at which we became a miner (according to current working ledger) or the current miner info was updated.
    u64 minerHeight = this->mMinerHeight;

    // we *are* authorized to mine, so find the height of the common root of the incoming branch and the working ledger branch.
    // if then root is earlier than the height at which we became a miner, then we can't know if we're a miner in the incoming branch.
    // rather than risk mining an invalid block, we'll leave the branch unaltered.
    
    BlockTreeCursor root = BlockTreeCursor::findRoot ( *this->mLedgerTag, tail );
    if ( root.getHeight () < minerHeight ) return tail;
    
    // at this stage, we're allowed to mine and we know that we're authorized on this branch. so see if we can improve
    // the branch. we can only improve it if we find a place where we can legally append (or replace) a block with a
    // more charming block of our own. we'll need to consider the rewrite window (i.e. don't replace blocks older
    // than the rewrite limit). we'll also need to consider the block delay (i.e. don't append a block before its time).
    // if we can't find an improvement, return the branch as-is. but if we *can* find an improvement, go ahead and
    // append (or replace) with a "provisional" node (i.e. a node with a header but no block).
    
    // there is also a special case when appending after one of our own blocks: the block node must be complete (i.e. not
    // be provisional).

    BlockTreeCursor parent = tail;
    BlockTreeCursor child;
    BlockTreeCursor extendFrom; // this is the *parent* of the block to append (if any).
    
    while ( parent.hasHeader ()) {
    
        const BlockHeader& parentHeader = parent.getHeader ();
        
        // if parent is one of ours, but it isn't yet complete, stop the search.
        if (( parentHeader.getMinerID () == this->mMinerID ) && ( !parent.isComplete ())) break;
        
        // if enough time has elapsed since the parent was declared, we can consider replacing the child
        // with our own block (or appending a new block).
        if ( parentHeader.getNextTime () <= now ) {
            
            // check to see if our block would be more charming. if so, extend from the parent.
            if ( !child.hasHeader () || BlockHeader::compare ( parentHeader.getNextCharm ( this->mVisage ), child.getCharm ()) < 0 ) {
                extendFrom = parent;
            }
        }
        
        // don't replace any block earlier than the consensus height;
        if ( parentHeader.getHeight () < consensusHeight ) break;
        
        // we can only replace blocks within the rewrite window. if parent is outside of that, no point in continuing.
        if ( !parentHeader.isInRewriteWindow ( now )) break;
        
        // don't replace our own block; that would be silly.
        if ( parentHeader.getMinerID () == this->mMinerID ) break;
        
        // don't replace the block in which we became a miner.
        if ( parent.getHeight () <= minerHeight ) break;
        
        child = parent;
        parent = parent.getParent ();
    }

    if ( extendFrom.hasHeader ()) {
        // this is what updates tag, which may be the "improved" tag from another miner
        return this->mBlockTree->makeProvisional ( this->prepareProvisional ( extendFrom.getHeader (), now ));
    }
    return tail; // use the chain as-is.
}

//----------------------------------------------------------------//
bool Miner::isLazy () const {

    return ( this->mContributors.size () == 0 );
}

//----------------------------------------------------------------//
shared_ptr < Block > Miner::loadGenesisBlock ( string path ) {

    string bodyString = FileSys::loadFileAsString ( path );
    return make_shared < Block >( bodyString );
}

//----------------------------------------------------------------//
void Miner::loadKey ( string keyfile, string password ) {
    UNUSED ( password );

    // TODO: password

    this->mKeyPair.load ( keyfile );
    assert ( this->mKeyPair );
}

//----------------------------------------------------------------//
Miner::Miner () :
    mFlags ( DEFAULT_FLAGS ),
    mNeedsReport ( true ),
    mReportMode ( REPORT_NONE ),
    mBlockVerificationPolicy ( Block::VerificationPolicy::ALL ),
    mControlLevel ( CONTROL_NONE ),
    mConsensusLookaheadHeight ( DEFAULT_CONSENSUS_LOOKAHEAD_HEIGHT ),
    mNetworkSearch ( false ),
    mPersistFrequency ( 0 ),
    mRetryPersistenceCheck ( 0 ),
    mPersistenceSleep ( 0 ),
    mAcceptedRelease ( 0 ),
    mProducedRelease ( 0 ) {
    
    this->mLedgerTag.setName ( "working" );
    this->mBestBranchTag.setName ( "best" );
    
    MinerLaunchTests::checkEnvironment ();
    
    this->mTransactionQueue = make_shared < TransactionQueue >();
}

//----------------------------------------------------------------//
Miner::~Miner () {
}

//----------------------------------------------------------------//
LedgerResult Miner::persistBlockTreeSQLite ( SQLiteConfig config ) {
    
    if ( this->mPrefixFilename.size () == 0 ) return "Missing persistence path.";
    
    string journalModeString = config.mJournalMode == SQLiteConfig::JOURNAL_MODE_WAL ? "-jmwal" : "";
    this->mBlocksFilename = Format::write ( "%s-blocks-sqlite%s.db", this->mPrefixFilename.c_str (), journalModeString.c_str ());
    
    try {
        this->mBlockTree            = make_shared < SQLiteBlockTree >( this->mBlocksFilename, config );
        this->mBlockSearchPool      = make_shared < BlockSearchPool >( *this, *this->mBlockTree );
        this->mBlockTree->setCacheSize ( DEFAULT_BLOCK_TREE_CACHE_SIZE );
    }
    catch ( SQLiteBlockTreeUnsupportedVersionException ) {
    
        return "Unsupported SQLite block tree format; delete your persist-chain folder and re-sync.";
    }
    
    return true;
}

//----------------------------------------------------------------//
LedgerResult Miner::persistLedger ( shared_ptr < AbstractPersistenceProvider > provider, shared_ptr < const Block > genesisBlock ) {
    
    assert ( genesisBlock );
    assert ( this->mLedger == NULL );
    assert ( this->mPersistenceProvider == NULL );
    assert ( this->mBlockTree );
    
    this->mPersistenceProvider  = provider;
    
    VersionedStoreTag tag = this->mPersistenceProvider->restore ( "master" );
    shared_ptr < Ledger > ledger = make_shared < Ledger >( tag );
    
    shared_ptr < const Block > topBlock = ledger->getBlock ();
    
    if ( topBlock ) {
    
        assert ( ledger->getHeight () == ( topBlock->getHeight () + 1 ));
    
        this->mLedger = ledger;
        this->mBlockTree->affirmBlock ( this->mLedgerTag, topBlock );
        this->mBlockTree->tag ( this->mBestBranchTag, this->mLedgerTag );
    }
    
    this->setGenesis ( genesisBlock );
    this->composeChain ( *this->mBestBranchTag );
    this->saveChain ();
    
    if ( FileSys::exists ( this->mConfigFilename )) {
        FromJSONSerializer::fromJSONFile ( this->mConfig, this->mConfigFilename );
    }
    
    if ( FileSys::exists ( this->mMinersFilename )) {
    
        SerializableSet < string > minerURLs;
        FromJSONSerializer::fromJSONFile ( minerURLs, this->mMinersFilename );
        
        SerializableSet < string >::const_iterator urlIt = minerURLs.cbegin ();
        for ( ; urlIt != minerURLs.cend (); ++urlIt ) {
            this->affirmRemoteMiner ( *urlIt );
        }
    }
    
    return true;
}

//----------------------------------------------------------------//
LedgerResult Miner::persistLedgerDebugStringStore ( shared_ptr < const Block > genesisBlock ) {
    
    return this->persistLedger ( make_shared < DebugStringStore >(), genesisBlock );
}

//----------------------------------------------------------------//
LedgerResult Miner::persistLedgerSQLite ( shared_ptr < const Block > genesisBlock, SQLiteConfig config ) {
    
    if ( this->mPrefixFilename.size () == 0 ) return "Missing persistence path.";
    
    string journalModeString = config.mJournalMode == SQLiteConfig::JOURNAL_MODE_WAL ? "-jmwal" : "";
    this->mLedgerFilename = Format::write ( "%s-sqlite%s.db", this->mPrefixFilename.c_str (), journalModeString.c_str ());
    
    return this->persistLedger ( SQLitePersistenceProvider::make ( this->mLedgerFilename, config ), genesisBlock );
}

//----------------------------------------------------------------//
LedgerResult Miner::persistLedgerSQLiteStringStore ( shared_ptr < const Block > genesisBlock, SQLiteConfig config ) {
    
    if ( this->mPrefixFilename.size () == 0 ) return "Missing persistence path.";
    
    string journalModeString = config.mJournalMode == SQLiteConfig::JOURNAL_MODE_WAL ? "-jmwal" : "";
    this->mLedgerFilename = Format::write ( "%s-sqlite-stringstore%s.db", this->mPrefixFilename.c_str (), journalModeString.c_str ());
    
    return this->persistLedger ( SQLiteStringStore::make ( this->mLedgerFilename, config ), genesisBlock );
}

//----------------------------------------------------------------//
shared_ptr < Block > Miner::prepareBlock ( time_t now ) {
        
    shared_ptr < const Block > prevBlock = this->mLedger->getBlock ();
    assert ( prevBlock );
    
    shared_ptr < Block > block = make_shared < Block >();
    block->initialize (
        this->mMinerID,
        this->mProducedRelease,
        this->mVisage,
        now,
        prevBlock.get (),
        this->mKeyPair
    );
    
    block->setBlockDelayInSeconds ( this->mLedger->getBlockDelayInSeconds ());
    block->setRewriteWindow ( this->mLedger->getRewriteWindowInSeconds ());
    
    this->mTransactionQueue->fillBlock ( *this->mLedger, *block, this->mBlockVerificationPolicy, this->getMinimumGratuity ());
    
    if ( !( this->isLazy () && ( block->countTransactions () == 0 ))) {
        block->setReward ( this->mLedger->chooseReward ( this->getReward ()));
        block->sign ( this->mKeyPair, Digest::DEFAULT_HASH_ALGORITHM );
        return block;
    }
    return NULL;
}

//----------------------------------------------------------------//
shared_ptr < BlockHeader > Miner::prepareProvisional ( const BlockHeader& parent, time_t now ) const {
    
    shared_ptr < BlockHeader > provisional = make_shared < BlockHeader >();
    provisional->initialize (
        this->mMinerID,
        this->mProducedRelease, // doesn't matter for provisional
        this->mVisage,
        now,
        &parent,
        this->mKeyPair
    );
    
    provisional->setBlockDelayInSeconds ( this->mLedger->getBlockDelayInSeconds ());
    provisional->setRewriteWindow ( this->mLedger->getRewriteWindowInSeconds ());
    provisional->setDigest ( Digest ( *provisional ));
    
    return provisional;
}

//----------------------------------------------------------------//
void Miner::pushBlock ( shared_ptr < const Block > block ) {

    assert ( this->mLedger->countBlocks () == block->getHeight ());

    this->mLedger->revertAndClear ( block->getHeight ());

    LedgerResult result = this->mLedger->pushBlock ( *block, this->mBlockVerificationPolicy );
    result.reportWithAssert ();
    
    BlockTreeCursor node = this->mBlockTree->affirmBlock ( this->mLedgerTag, block );
    assert ( node.hasHeader ());
}

//----------------------------------------------------------------//
void Miner::report () const {

    this->report ( this->mReportMode );
}

//----------------------------------------------------------------//
void Miner::report ( ReportMode reportMode ) const {

    LGN_LOG_SCOPE ( VOL_FILTER_MINING_REPORT, INFO, __PRETTY_FUNCTION__ );

    BlockTreeCursor ledgerCursor = *this->mBestBranchTag;

    switch ( reportMode ) {
    
        case REPORT_BEST_BRANCH: {
        
            LGN_LOG ( VOL_FILTER_MINING_REPORT, INFO, "%d: %s", ( int )ledgerCursor.getHeight (), ledgerCursor.writeBranch ().c_str ());
            break;
        }
        
        case REPORT_ALL_BRANCHES: {
                    
            u64 maxHeight = ledgerCursor.getHeight () + 1;
                        
            map < string, shared_ptr < RemoteMiner >>::const_iterator remoteMinerIt = this->mRemoteMinersByURL.begin ();
            for ( ; remoteMinerIt != this->mRemoteMinersByURL.end (); ++remoteMinerIt ) {
                shared_ptr < RemoteMiner > remoteMiner = remoteMinerIt->second;
                
                BlockTreeCursor remoteCursor = *remoteMinerIt->second->mTag;
                 if ( remoteCursor.hasHeader ()) {
                    u64 tailHeight = remoteCursor.getHeight () + 1;
                    maxHeight = maxHeight < tailHeight ? tailHeight : maxHeight;
                }
            }
            
            static const u64 REPORT_LENGTH = 6;
            u64 minHeight = ( maxHeight < REPORT_LENGTH ) ? 0 : maxHeight - REPORT_LENGTH;
            
            remoteMinerIt = this->mRemoteMinersByURL.begin ();
            for ( ; remoteMinerIt != this->mRemoteMinersByURL.end (); ++remoteMinerIt ) {
                remoteMinerIt->second->report ( minHeight, maxHeight );
            }
            
            LGN_LOG ( VOL_FILTER_MINING_REPORT, INFO, "BEST - %d: %s", ( int )maxHeight - 1, ledgerCursor.writeBranch ( minHeight, maxHeight ).c_str ());
            LGN_LOG ( VOL_FILTER_MINING_REPORT, INFO, "BLOCK SEARCHES: %d", ( int )this->mBlockSearchPool->countSearches ());
            LGN_LOG ( VOL_FILTER_MINING_REPORT, INFO, "ACTIVE SEARCHES: %d", ( int )this->mBlockSearchPool->countActiveSearches ());
            LGN_LOG ( VOL_FILTER_MINING_REPORT, INFO, "LEDGER TAG: %s", this->getLedgerTag ().write ().c_str ());
            break;
        }
        
        case REPORT_NONE:
        default:
            break;
    }
}

//----------------------------------------------------------------//
void Miner::reset () {

    this->mTransactionQueue->reset ();
    this->mLedger->revertAndClear ( 1 );
    this->mLedger->clearSchemaCache ();
    this->Miner_reset ();
}

//----------------------------------------------------------------//
set < shared_ptr < RemoteMiner >> Miner::sampleContributors ( size_t sampleSize ) const {

    return UnsecureRandom::get ().sampleSet < shared_ptr < RemoteMiner >> ( this->mContributors, sampleSize );
}

//----------------------------------------------------------------//
set < shared_ptr < RemoteMiner >> Miner::sampleOnlineMiners ( size_t sampleSize ) const {

    return UnsecureRandom::get ().sampleSet < shared_ptr < RemoteMiner >> ( this->mOnlineMiners, sampleSize );
}

//----------------------------------------------------------------//
void Miner::saveChain () {
    
    if ( !( this->mLedger && this->mPersistenceProvider )) return;
    
    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    LGN_LOG ( VOL_FILTER_STORE, INFO, "PADAMOSE (BEFORE PERSIST):" );
    this->mLedger->printTree ( VOL_FILTER_STORE );

    assert ( this->checkTags ());
    this->mPersistenceProvider->persist ( *this->mLedger, "master" );
    
    LGN_LOG ( VOL_FILTER_STORE, INFO, "PADAMOSE (AFTER PERSIST):" );
    this->mLedger->printTree ( VOL_FILTER_STORE );
    
    if ( this->mPersistenceSleep ) {
        Poco::Thread::sleep (( long )this->mPersistenceSleep );
    }
    
    BlockTreeCursor ledgerCursor = *this->mLedgerTag;
    
    LGN_LOG ( VOL_FILTER_STORE, INFO, "LEDGER BLOCK COUNT: %d", ( int )this->mLedger->countBlocks ());
    LGN_LOG ( VOL_FILTER_STORE, INFO, "BLOCK TREE COUNT: %d", ( int )( ledgerCursor.getHeight () + 1 ));
    
    LGN_LOG ( VOL_FILTER_STORE, INFO, "HASH FROM BLOCK TREE: %s", ledgerCursor.getHash ().c_str ());
    LGN_LOG ( VOL_FILTER_STORE, INFO, "HEIGHT FROM BLOCK TREE: %d", ( int )ledgerCursor.getHeight ());
    
    shared_ptr < const Block > ledgerBlock = this->mLedger->getBlock ();
    
    if ( ledgerBlock ) {
        LGN_LOG ( VOL_FILTER_STORE, INFO, "HASH FROM LEDGER: %s", ledgerBlock->getDigest ().toHex ().c_str ());
        LGN_LOG ( VOL_FILTER_STORE, INFO, "HEIGHT FROM LEDGER: %d", ( int )ledgerBlock->getHeight ());
    }
        
    if ( !( ledgerBlock && ledgerBlock->equals ( ledgerCursor ))) {
    
        this->mLedger->printVersion ( ledgerCursor.getHeight (), VOL_FILTER_STORE );
    
        shared_ptr < const Block > ledgerBlockFromHash = this->mLedger->getBlock ( ledgerCursor.getHash ());
        if ( ledgerBlockFromHash ) {
            LGN_LOG ( VOL_FILTER_STORE, ERROR, "LEDGER BLOCK *WAS* FOUND FOR HASH" );
        }
        
        LGN_LOG ( VOL_FILTER_STORE, ERROR, "LEDGER BLOCK IS MISSING OR PERSISTED LEDGER DOESN'T MATCH LEDGER TAG" );
    
        size_t retry = this->mRetryPersistenceCheck;
        bool recovered = false;
        
        for ( size_t i = 0; i < retry; ++i ) {
            LGN_LOG_SCOPE ( VOL_FILTER_STORE, ERROR, "RETRYING PERSISTENCE INTEGRITY CHECK..." );
            
            Poco::Thread::sleep (( long )( this->mPersistenceSleep ? this->mPersistenceSleep : 100 ));
            
            ledgerBlock = this->mLedger->getBlock ();
            
            if ( ledgerBlock && ledgerBlock->equals ( ledgerCursor )) {
                LGN_LOG ( VOL_FILTER_STORE, ERROR, " SUCCESS!" );
                recovered = true;
            }
            else {
                LGN_LOG ( VOL_FILTER_STORE, ERROR, " NOPE" );
            }
        }
        
        if ( !recovered ) {
        
            LGN_LOG_SCOPE ( VOL_FILTER_STORE, ERROR, "BLOCK MISSING OR INCOMPLETE AT HEIGHT %d", ( int )ledgerCursor.getHeight ());
            BlockODBM blockODBM ( *this->mLedger, ledgerCursor.getHeight ());
            
            if ( blockODBM.mHeader.exists ()) {
                LGN_LOG ( VOL_FILTER_STORE, ERROR, "blockODBM.mHeader.exists () == true" );
            }
            
            if ( blockODBM.mHash.exists ()) {
                LGN_LOG ( VOL_FILTER_STORE, ERROR, "blockODBM.mHash.exists () == true" );
            }
            
            if ( blockODBM.mBlock.exists ()) {
                LGN_LOG ( VOL_FILTER_STORE, ERROR, "blockODBM.mBlock.exists () == true" );
            }
        }
        assert ( recovered );
    }
}

//----------------------------------------------------------------//
void Miner::saveConfig () {

    if ( this->mConfigFilename.size () > 0 ) {
        ToJSONSerializer::toJSONFile ( this->mConfig, this->mConfigFilename );
    }
}

//----------------------------------------------------------------//
void Miner::scheduleReport () {

    this->mNeedsReport = true;
}

//----------------------------------------------------------------//
void Miner::setBlockTree ( shared_ptr < AbstractBlockTree > blockTree ) {

    this->mBlockTree            = blockTree ? blockTree : make_shared < InMemoryBlockTree >();
    this->mBlockSearchPool      = make_shared < BlockSearchPool >( *this, *this->mBlockTree );
}

//----------------------------------------------------------------//
void Miner::setBlockTreeCacheSize ( size_t cacheSize ) {

    this->mBlockTree->setCacheSize ( cacheSize );
}

//----------------------------------------------------------------//
void Miner::setConsensusLookaheadHeight ( size_t height ) {

    this->mConsensusLookaheadHeight = height;
}

//----------------------------------------------------------------//
void Miner::setGenesis ( shared_ptr < const Block > block ) {

    assert ( block );
    assert ( this->mBlockTree );

    if ( this->mLedger ) {
    
        assert ( this->mLedgerTag.hasCursor ());
        assert ( this->mLedger->countBlocks ());
        assert ( this->mLedger->getGenesisHash () == block->getDigest ().toHex ());
    }
    else {
    
        this->mLedger = make_shared < Ledger >();
        this->pushBlock ( block );
        this->mBlockTree->tag ( this->mBestBranchTag, this->mLedgerTag );
    }
    
    this->updateRelease ();
    this->updateMinerStatus ();
}

//----------------------------------------------------------------//
void Miner::setMaxBlockSearches ( size_t max ) {

    assert ( this->mBlockSearchPool );
    this->mBlockSearchPool->setMaxSearches ( max );
}

//----------------------------------------------------------------//
void Miner::setMinimumGratuity ( u64 minimumGratuity ) {

    this->mConfig.mMinimumGratuity = minimumGratuity;
    this->saveConfig ();
}

//----------------------------------------------------------------//
void Miner::setMute ( bool paused ) {

    this->mFlags = SET_BITS ( this->mFlags, MINER_MUTE, paused );
}

//----------------------------------------------------------------//
void Miner::setPersistencePath ( string path, shared_ptr < const Block > genesisBlock ) {
    
    assert ( genesisBlock );
    
    FileSys::createDirectories ( path );

    string hash             = genesisBlock->getDigest ().toHex ();
    this->mPrefixFilename   = Format::write ( "%s/%s%s", path.c_str (), PERSIST_PREFIX, hash.c_str ());
    
    this->mConfigFilename   = Format::write ( "%s-config.json", this->mPrefixFilename.c_str ());
    this->mMinersFilename   = Format::write ( "%s-miners.json", this->mPrefixFilename.c_str ());
}

//----------------------------------------------------------------//
void Miner::setReward ( string reward ) {

    this->mConfig.mReward = reward;
    this->saveConfig ();
}

//----------------------------------------------------------------//
void Miner::setVerbose ( bool verbose ) {

    this->mFlags = SET_BITS ( this->mFlags, MINER_VERBOSE, verbose );
}

//----------------------------------------------------------------//
void Miner::shutdown ( bool kill ) {

    this->Miner_shutdown ( kill );
}

//----------------------------------------------------------------//
void Miner::step ( time_t now ) {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    this->affirmMessenger ();
    this->mMessenger->receiveResponses ( *this, now );
    this->updateRemoteMinerGroups ();
    this->updateRemoteMiners ();
    this->updateBlockSearches ();
    this->updateNetworkSearches ();
  
    this->updateBestBranch ( now );
    this->saveChain ();
    
    this->updateRelease ();
    this->updateMinerStatus ();
    
    try {
        this->mMessenger->sendRequests ();
    }
    catch ( Poco::Exception& exc ) {
        LGN_LOG ( VOL_FILTER_CONSENSUS, INFO, "Caught exception in MinerActivity::runActivity ()" );
    }
}

//----------------------------------------------------------------//
void Miner::updateBestBranch ( time_t now ) {

    // first, sample all the branches, including the current "best" branch.
    // trim missing and invalid segments. the sampler will find the consensus
    // subtree and also contain a list of branch cursors for future consideration as "best."

    BlockTreeSampler sampler;
    
    sampler.addLeaf (( *this->mBestBranchTag ).trimMissingOrInvalidBranch ());
    
    set < shared_ptr < RemoteMiner >>::iterator remoteMinerIt = this->mOnlineMiners.begin ();
    for ( ; remoteMinerIt != this->mOnlineMiners.end (); ++remoteMinerIt ) {
        
        shared_ptr < RemoteMiner > remoteMiner = *remoteMinerIt;
        if ( !remoteMiner->mTag.hasCursor ()) continue;
        
        sampler.addLeaf (( *remoteMiner->mTag ).trimMissingOrInvalidBranch ());
    }
    
    BlockTreeSamplerNode subTree = sampler.sample ();
    
    // iterate through each branch in the sampler; consider it if it is equal to
    // or greater than the consensus height. if "improving," do not rewind
    // past the consensus height.
    
    BlockTreeCursor bestCursor;
    u64 consensusHeight = subTree.mRoot.getHeight ();
    
    list < BlockTreeCursor >::const_iterator branchIt = sampler.mLeaves.cbegin ();
    for ( ; branchIt != sampler.mLeaves.cend (); ++branchIt ) {
        
        if ( branchIt->getHeight () < consensusHeight ) continue; // skip child nodes below the consensus height.
        
        BlockTreeCursor challengeCursor = this->improveBranch ( *branchIt, consensusHeight, now );
        
        assert ( consensusHeight <= challengeCursor.getHeight ());
        
        if ( !bestCursor.hasHeader () || ( BlockTreeCursor::compare ( challengeCursor, bestCursor ) < 0 )) {
            bestCursor = challengeCursor;
        }
    }
    
    assert ( bestCursor.hasHeader ());
    this->mBlockTree->tag ( this->mBestBranchTag, bestCursor );
    
    this->composeChain ( this->mBestBranchTag.getCursor ());
    
    // if the next block is provisional (i.e. is ours, waiting to be produced).
    if (
        bestCursor.isProvisional () &&                                  // end of best chain is provisional
        ( bestCursor.getHeight () == this->mLedger->countBlocks ()) &&  // ledger is complete and caught up (i.e. previous to end of chain)
        (
            subTree.mRoot.isProvisional () ||                           // subtree is provisional (solo miner) OR
            subTree.mRoot.equals ( this->mLedgerTag.getCursor ())       // ledger is also consensus root
        )
    ) {

        this->mTransactionQueue->pruneTransactions ( *this->mLedger );

        // prepare block may return an empty block if there's no mining network visible and there are no transactions.
        shared_ptr < Block > block = this->prepareBlock ( now );
        if ( block ) {
                              
            // push the block, which will also update the ledger tag.
            this->pushBlock ( block );
            
            // re-tag the best branch; this replaces the provisional block with our new one.
            this->mBlockTree->tag ( this->mBestBranchTag, this->mLedgerTag );
        }
    }

    // TODO: if consensus root is an ancestor of ledger tag, purge transactions using consensus root
}

//----------------------------------------------------------------//
void Miner::updateBlockSearches () {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    BlockSearchPool& blockSearchPool = *this->mBlockSearchPool;

    set < shared_ptr < RemoteMiner >>::const_iterator remoteMinerIt = this->mOnlineMiners.begin ();
    for ( ; remoteMinerIt != this->mOnlineMiners.end (); ++remoteMinerIt ) {
        
        shared_ptr < const RemoteMiner > remoteMiner = *remoteMinerIt;

        // we only care about missing branches; ignore new/complete/invalid branches.
        if ( remoteMiner->mTag.hasCursor () && ( *remoteMiner->mTag ).isMissing ()) {
            blockSearchPool.affirmBranchSearch ( *remoteMiner->mTag );
        }
    }
    
    // always affirm a search for the current branch
    blockSearchPool.affirmBranchSearch ( *this->mBestBranchTag );
    
    blockSearchPool.update ();
}

//----------------------------------------------------------------//
void Miner::updateMinerStatus () {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    // TODO: this is a hack to speed up the certain queries

    this->mSnapshotMutex.lock ();
    
    {
        LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, "SNAPSHOT" );
    
        Ledger& ledger = *this->mLedger;
        ledger.getSchema ();
        
        AccountODBM accountODBM ( ledger, this->getMinerID ());
        
        this->mIsMiner = accountODBM.isMiner ();
        
        if ( this->mIsMiner ) {

            this->mMinerHeight = accountODBM.mMinerHeight.get ();
            
            shared_ptr < const MinerInfo > minerInfo = accountODBM.mMinerInfo.get ();
            this->mMotto    = minerInfo->getMotto ();
            this->mVisage   = minerInfo->getVisage ();
        }
        
        this->mMinerStatus.mSchemaVersion           = ledger.getSchemaVersion ();
        this->mMinerStatus.mSchemaHash              = ledger.getSchemaHash ();
        this->mMinerStatus.mGenesisHash             = ledger.getGenesisHash ();
        this->mMinerStatus.mIdentity                = ledger.getIdentity ();
        this->mMinerStatus.mMinimumGratuity         = this->getMinimumGratuity ();
        this->mMinerStatus.mReward                  = this->getReward ();
        this->mMinerStatus.mTotalBlocks             = ledger.countBlocks ();
        this->mMinerStatus.mFeeSchedule             = ledger.getTransactionFeeSchedule ();
        this->mMinerStatus.mMonetaryPolicy          = ledger.getMonetaryPolicy ();
        this->mMinerStatus.mPayoutPolicy            = ledger.getPayoutPolicy ();
        this->mMinerStatus.mMinerBlockCount         = accountODBM.mMinerBlockCount.get ( 0 );
        
        this->mMinerStatus.mRewardPool              = ledger.getRewardPool ();
        this->mMinerStatus.mPrizePool               = ledger.getPrizePool ();
        this->mMinerStatus.mPayoutPool              = ledger.getPayoutPool ();
        this->mMinerStatus.mVOL                     = ledger.countVOL ();

        this->mMinerStatus.mAcceptedRelease         = this->mAcceptedRelease;

        this->mSnapshot = *this;
    }

    this->mSnapshotMutex.unlock ();
    
    this->mLockedLedgerMutex.lock ();
    {
        LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, "Ledger LOCK" );
        this->mLockedLedger.lock ( *this->mLedger );
        this->mLockedLedger.mSchemaCache = this->mLedger->mSchemaCache;
    }
    this->mLockedLedgerMutex.unlock ();
}

//----------------------------------------------------------------//
void Miner::updateNetworkSearches () {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    // poll network for miner URLs
    if ( !this->mNetworkSearch ) {
        set < shared_ptr < RemoteMiner >> miners = this->sampleOnlineMiners ( 1 );
        if ( miners.size ()) {
            this->mMessenger->enqueueExtendNetworkRequest (( *miners.cbegin ())->getURL ());
            this->mNetworkSearch = true;
        }
    }
}

//----------------------------------------------------------------//
void Miner::updateRelease () {

    // the "release" is the current build of the node. it governs what transactions are valid
    // and how those transactions are applied. we only want to create blocks with the current release
    // when (a) most miners *can* accept them and (b) most miners are *ready* to accept them. we don't
    // want to create blocks with the current release until we're confident that most other miners
    // will both accept and create them.

    // if we're accepting and producing the node's release, nothing else to do.
    if (( this->mAcceptedRelease == VOL_NODE_RELEASE ) && ( this->mProducedRelease == VOL_NODE_RELEASE )) return;

    u64 ledgerRelease = this->mLedger->getRelease ();

    // by definition, the 'accepted release' *must* be equal to or greater than the last block recorded.
    if ( this->mAcceptedRelease < ledgerRelease ) {
    
        // also by definition, the 'produced release' *must* be equal or greater than the 'accepted release.'
        this->mProducedRelease = ledgerRelease;
        this->mAcceptedRelease = ledgerRelease;
    }

    u64 totalMiners = this->mOnlineMiners.size () + 1; // total miners, including self
    if ( totalMiners < 4 ) return; // TODO: delay until network is reasonably known (i.e. new miners no longer being discovered)
    
    u64 totalMinersForAccepting = 1;
    u64 totalMinersForProducing = 1;
    
    set < shared_ptr < RemoteMiner >>::const_iterator remoteMinerIt = this->mOnlineMiners.begin ();
    for ( ; remoteMinerIt != this->mOnlineMiners.end (); ++remoteMinerIt ) {
        shared_ptr < const RemoteMiner > remoteMiner = *remoteMinerIt;

        if ( remoteMiner->mNextRelease >= VOL_NODE_RELEASE ) {
            totalMinersForAccepting++;
        }
        
        if ( remoteMiner->mAcceptedRelease >= VOL_NODE_RELEASE ) {
            totalMinersForProducing++;
        }
    }
    
    // if most other miners have a NEXT release matching or exceeding this miner's NEXT release, update the ACCEPTED release.
    if ((( double )totalMinersForAccepting / ( double )totalMiners ) >= 0.72 ) {
        this->mAcceptedRelease = VOL_NODE_RELEASE;
    }

    // if most other miners have an ACCEPTED release matching or exceeding this miner's NEXT release, update the PRODUCED release.
    if ((( double )totalMinersForProducing / ( double )totalMiners ) >= 0.72 ) {
        this->mProducedRelease = VOL_NODE_RELEASE;
    }
}

//----------------------------------------------------------------//
void Miner::updateRemoteMinerGroups () {

    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );

    this->mOnlineMiners.clear ();
    this->mOnlineMinerURLs.clear ();
    
    this->mContributors.clear ();
    
    set < shared_ptr < RemoteMiner >>::iterator remoteMinerIt = this->mRemoteMiners.begin ();
    for ( ; remoteMinerIt != this->mRemoteMiners.end (); ++remoteMinerIt ) {
    
        shared_ptr < RemoteMiner > remoteMiner = *remoteMinerIt;
        
        // TODO: detect and exclude blocks from non-miners
//        AccountODBM accountODBM ( *this->mLedger, remoteMiner->getMinerID ());
//        if ( !accountODBM.isMiner ()) continue;
        
        if ( remoteMiner->isOnline ()) {
            this->mOnlineMiners.insert ( remoteMiner );
            this->mOnlineMinerURLs.insert ( remoteMiner->getURL ());
        }
        
        if ( remoteMiner->isContributor ()) {
            this->mContributors.insert ( remoteMiner );
        }
    }
}

//----------------------------------------------------------------//
void Miner::updateRemoteMiners () {
    
    LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, __PRETTY_FUNCTION__ );
    
    LGN_LOG ( VOL_FILTER_CONSENSUS, INFO, "affirming remote miner new URLs" );
    
    // get miner URL list from ledger
    set < string > miners = this->getLedger ().getMiners ();
    set < string >::iterator minerIt = miners.begin ();
    for ( ; minerIt != miners.end (); ++minerIt ) {
        
        string minerID = *minerIt;
        if ( minerID == this->mMinerID ) continue;
        
        AccountODBM minerODBM ( this->getLedger (), *minerIt );
        string url = minerODBM.mMinerInfo.get ()->getURL ();
        
        this->affirmRemoteMiner ( url );
    }
    
    LGN_LOG ( VOL_FILTER_CONSENSUS, INFO, "affirming remote miner pending URLs" );
    
    // affirm miners for pending URLs
    while ( this->mNewMinerURLs.size ()) {
        
        string url = *this->mNewMinerURLs.begin ();
        
        if ( this->mCompletedURLs.find ( url ) == this->mCompletedURLs.cend ()) {
            
            shared_ptr < RemoteMiner > remoteMiner = make_shared < RemoteMiner >( *this );
            remoteMiner->mURL = url;
            this->mRemoteMinersByURL [ url ] = remoteMiner;
            this->mRemoteMiners.insert ( remoteMiner );
            this->mCompletedURLs.insert ( url );
        }
        this->mNewMinerURLs.erase ( url );
    }
    
    SerializableSet < string > minerURLs;
    
    LGN_LOG ( VOL_FILTER_CONSENSUS, INFO, "updating remote miner state" );
    
    size_t lookahead = this->mLedger->getHeight () + this->mConsensusLookaheadHeight;
    
    // update miner state
    set < shared_ptr < RemoteMiner >>::iterator remoteMinerIt = this->mRemoteMiners.begin ();
    for ( ; remoteMinerIt != this->mRemoteMiners.end (); ++remoteMinerIt ) {
        shared_ptr < RemoteMiner > remoteMiner = *remoteMinerIt;
        remoteMiner->update ( this->mAcceptedRelease, lookahead );
        minerURLs.insert ( remoteMiner->getURL ());
    }
    
    if ( this->mMinersFilename.size ()) {
        LGN_LOG_SCOPE ( VOL_FILTER_CONSENSUS, INFO, "writing miners.json" );
        ToJSONSerializer::toJSONFile ( minerURLs, this->mMinersFilename );
    }
}

//================================================================//
// overrides
//================================================================//

//----------------------------------------------------------------//
void Miner::AbstractMiningMessengerClient_receiveResponse ( const MiningMessengerResponse& response, time_t now ) {

    this->scheduleReport ();

    const MiningMessengerRequest& request   = response.mRequest;
    string url                              = response.mRequest.mMinerURL;
    
    map < string, shared_ptr < RemoteMiner >>::iterator remoteMinerIt = this->mRemoteMinersByURL.find ( url );
    shared_ptr < RemoteMiner > remoteMiner = remoteMinerIt != this->mRemoteMinersByURL.cend () ? remoteMinerIt->second : NULL;
    
    // TODO: need to verify miners are who they say (return signed timestamp or challenge)
    
    if ( !remoteMiner ) return;
    
    // TODO: could be set deliberately as an attack (eacy to knock other miners out of circulation this way)
    if ( response.mMinerID == this->mMinerID ) {
        this->mRemoteMinersByURL.erase ( url );
        this->mRemoteMiners.erase ( remoteMiner );
        return;
    }
    
    switch ( request.mRequestType ) {
        
        case MiningMessengerRequest::REQUEST_BLOCK: {
            
            if ( response.mBlock ) {
                this->mBlockTree->update ( response.mBlock );
            }
            string hash = request.mBlockDigest.toHex ();
            this->mBlockSearchPool->updateBlockSearch ( remoteMiner->getMinerID (), hash, ( bool )response.mBlock );
            break;
        }
        
        case MiningMessengerRequest::REQUEST_EXTEND_NETWORK: {
            
            set < string >::const_iterator urlIt = response.mMinerURLs.cbegin ();
            for ( ; urlIt != response.mMinerURLs.cend (); ++urlIt ) {
                this->affirmRemoteMiner ( *urlIt );
            }
            this->mNetworkSearch = false;
            break;
        }
        
        default: {
            break;
        }
    }
    
    remoteMiner->receiveResponse ( response, now, this->mAcceptedRelease );
}

//----------------------------------------------------------------//
void Miner::AbstractSerializable_serializeFrom ( const AbstractSerializerFrom& serializer ) {
    UNUSED ( serializer );
}

//----------------------------------------------------------------//
void Miner::AbstractSerializable_serializeTo ( AbstractSerializerTo& serializer ) const {
    UNUSED ( serializer );
}

//----------------------------------------------------------------//
void Miner::Miner_reset () {
}

//----------------------------------------------------------------//
void Miner::Miner_shutdown ( bool kill ) {
    UNUSED ( kill );

    // explicitly release messenger and possibly trigger shutdown
    this->mMessenger = NULL;
}

} // namespace Volition
