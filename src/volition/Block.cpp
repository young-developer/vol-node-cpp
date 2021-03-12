// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/Block.h>
#include <volition/BlockODBM.h>
#include <volition/CryptoKey.h>
#include <volition/Format.h>
#include <volition/Ledger.h>
#include <volition/TheTransactionBodyFactory.h>
#include <volition/Transaction.h>

namespace Volition {

//================================================================//
// Block
//================================================================//

//----------------------------------------------------------------//
void Block::affirmHash () {

    if ( !this->mDigest ) {
        this->mDigest = Digest ( *this );
    }
}

//----------------------------------------------------------------//
LedgerResult Block::apply ( Ledger& ledger, VerificationPolicy policy ) const {

    if ( ledger.getVersion () != this->mHeight ) return "Apply block: height/version mismatch.";
    
    if ( this->mHeight == 0 ) {
        ledger.init ();
    }
    
    LedgerResult verifyResult = this->verify ( ledger, policy );
    if ( !verifyResult ) return verifyResult;

    // some transactions need to be applied later.
    // we need to evaluate if they are legal now.
    // then process them once we have the entropy.
    
    // before applying the block, we need to apply the entropy.
    // then, get the list of blocks with transactions due on the current version.
    // apply those transactions and remove them from the pending list.
    // then, as we push the block, if it has pending transactions, add them.
    // if a block is removed or added from the list, flag it.
    // if it's been flagged, record it in the ledger at the end.

    // apply the entropy up front.
    this->applyEntropy ( ledger );

    // process unfinished blocks.
    UnfinishedBlockList unfinished = ledger.getUnfinished ();
    bool unfinishedChanged = false;
    
    UnfinishedBlockList nextUnfinished;
    UnfinishedBlockList::Iterator unfinishedBlockIt = unfinished.mBlocks.cbegin ();
    for ( ; unfinishedBlockIt != unfinished.mBlocks.end (); ++unfinishedBlockIt ) {
        UnfinishedBlock unfinishedBlock = *unfinishedBlockIt;
        
        if ( unfinishedBlock.mMaturity == this->mHeight ) {
            
            shared_ptr < const Block > block = ledger.getBlock ( unfinishedBlock.mBlockID );
            assert ( block );
            
            size_t nextMaturity;
            LedgerResult transactionsResult = block->applyTransactions ( ledger, policy, nextMaturity );
            if ( !transactionsResult ) return transactionsResult;
            
            if ( nextMaturity > this->mHeight ) {
            
                unfinishedBlock.mMaturity = nextMaturity;
                nextUnfinished.mBlocks.push_back ( unfinishedBlock );
            }
            
            unfinishedChanged = true;
        }
    }

    // apply transactions
    size_t nextMaturity;
    LedgerResult transactionsResult = this->applyTransactions ( ledger, policy, nextMaturity );
    if ( !transactionsResult ) return transactionsResult;
    
    if ( nextMaturity > this->mHeight ) {
    
        UnfinishedBlock unfinishedBlock;
        unfinishedBlock.mBlockID = this->mHeight;
        unfinishedBlock.mMaturity = nextMaturity;
        nextUnfinished.mBlocks.push_back ( unfinishedBlock );
        
        unfinishedChanged = true;
    }
    
    // check pending block list, and apply if changed.
    if ( unfinishedChanged ) {
        ledger.setUnfinished ( nextUnfinished );
    }
        
    BlockODBM blockODBM ( ledger, this->mHeight );
    
    string hash = this->mDigest.toHex ();
    
    blockODBM.mHash.set ( this->mDigest.toHex ());
    blockODBM.mPose.set ( this->mPose.toHex ());
    blockODBM.mHeader.set ( *this );
    blockODBM.mBlock.set ( *this );

    return true;
}

//----------------------------------------------------------------//
LedgerResult Block::applyTransactions ( Ledger& ledger, VerificationPolicy policy, size_t& nextMaturity ) const {

    nextMaturity = this->mHeight;
    size_t height = ledger.getVersion ();

    AccountODBM accountODBM ( ledger, this->mMinerID );
    assert ( accountODBM || ledger.isGenesis ());

    size_t gratuity         = 0;
    size_t profitShare      = 0;
    size_t transferTax      = 0;

    if ( ledger.getVersion () >= this->mHeight ) {
        
        // apply block transactions.
        for ( size_t i = 0; i < this->mTransactions.size (); ++i ) {
            const Transaction& transaction = *this->mTransactions [ i ];
            
            size_t transactionMaturity = this->mHeight + transaction.getMaturity ();
            if ( transactionMaturity == height ) {
            
                TransactionResult result = transaction.apply ( ledger, this->mTime, policy );
                if ( !result ) return Format::write ( "%s: %s", result.getUUID ().c_str (), result.getMessage ().c_str ());
                
                gratuity        += transaction.getGratuity ();
                profitShare     += transaction.getProfitShare ();
                transferTax     += transaction.getTransferTax ();
            }
            
            if ( nextMaturity < transactionMaturity ) {
                nextMaturity = transactionMaturity;
            }
        }
    }
    
    ledger.invokeReward ( this->mMinerID, this->mReward, this->mTime );
    
    if ( accountODBM && ( gratuity > 0 )) {
        Account accountUpdated = *accountODBM.mBody.get ();
        accountUpdated.mBalance += gratuity - profitShare;
        accountODBM.mBody.set ( accountUpdated );
    }
    
    ledger.distribute ( transferTax );
    
    return true;
}

//----------------------------------------------------------------//
Block::Block () {
}

//----------------------------------------------------------------//
Block::~Block () {
}

//----------------------------------------------------------------//
size_t Block::countTransactions () const {

    return this->mTransactions.size ();
}

//----------------------------------------------------------------//
size_t Block::getWeight () const {

    size_t weight = 0;
    for ( size_t i = 0; i < this->mTransactions.size (); ++i ) {
        weight += this->mTransactions [ i ]->getWeight ();
    }
    return weight;
}

//----------------------------------------------------------------//
void Block::pushTransaction ( shared_ptr < const Transaction > transaction ) {

    this->mTransactions.push_back ( transaction );
}

//----------------------------------------------------------------//
const Digest& Block::sign ( const CryptoKeyPair& key, string hashAlgorithm ) {
        
    this->mDigest = Digest ( *this );
    this->mSignature = key.sign ( this->mDigest, hashAlgorithm );
    return this->mSignature.getSignature ();
}

//----------------------------------------------------------------//
LedgerResult Block::verify ( const Ledger& ledger, VerificationPolicy policy ) const {

    if ( this->mHeight == 0 ) {
        BlockODBM genesisODBM ( ledger, 0 );
        if ( genesisODBM.mHash.get () != this->mDigest.toHex ()) return "Verify block: genesis hash mismatch.";
        return true;
    }
    else {
    
        BlockODBM parentODBM ( ledger, this->mHeight - 1 );
        if ( parentODBM.mHash.get ( "" ) != this->mPrevDigest.toHex ()) return "Verify block: parent hash mismatch.";

        if ( this->mBlockDelay != ledger.getBlockDelayInSeconds ()) return "Verify block: block delay mismatch.";
        if ( this->mRewriteWindow != ledger.getRewriteWindowInSeconds ()) return "Verify block: rewrite window mismatch.";
        if ( this->getWeight () > ledger.getMaxBlockWeight ()) return "Verify block: max block weight exceeded.";
    }

    shared_ptr < const MinerInfo > minerInfo = AccountODBM ( ledger, this->mMinerID ).mMinerInfo.get ();
    if ( !minerInfo ) return "Verify block: miner info not found.";

    const CryptoPublicKey& key = minerInfo->getPublicKey ();

    if ( policy & ( VerificationPolicy::VERIFY_POSE | VerificationPolicy::VERIFY_CHARM )) {

        BlockODBM parentODBM ( ledger, this->mHeight - 1 );
        string prevPoseHex = parentODBM.mPose.get ();

        if ( policy & VerificationPolicy::VERIFY_POSE ) {
            Digest digest = this->hashPose ( prevPoseHex );
            if ( !key.verify ( this->mPose, digest )) return "Verify block: invalid POSE.";
        }

        if ( policy & VerificationPolicy::VERIFY_CHARM ) {
            
            Digest prevPose;
            prevPose.fromHex ( prevPoseHex );
            Digest charm = BlockHeader::calculateCharm ( prevPose, minerInfo->getVisage ());
            if ( this->mCharm != charm ) return "Verify block: invalid CHARM.";
        }
    }

    if ( policy & VerificationPolicy::VERIFY_BLOCK_SIG ) {
        if ( !key.verify ( this->mSignature, *this )) return "Verify block: invalid SIGNATURE.";
    }
    return true;
}

//================================================================//
// overrides
//================================================================//

//----------------------------------------------------------------//
void Block::AbstractSerializable_serializeFrom ( const AbstractSerializerFrom& serializer ) {
    BlockHeader::AbstractSerializable_serializeFrom ( serializer );
    
    serializer.serialize ( "reward",        this->mReward );
    serializer.serialize ( "transactions",  this->mTransactions );
    this->affirmHash ();
}

//----------------------------------------------------------------//
void Block::AbstractSerializable_serializeTo ( AbstractSerializerTo& serializer ) const {
    BlockHeader::AbstractSerializable_serializeTo ( serializer );
    
    serializer.serialize ( "reward",        this->mReward );
    serializer.serialize ( "transactions",  this->mTransactions );
}

} // namespace Volition
