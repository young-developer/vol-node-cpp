// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_MINER_H
#define VOLITION_MINER_H

#include <volition/common.h>
#include <volition/AbstractTransaction.h>
#include <volition/CryptoKey.h>
#include <volition/Chain.h>
#include <volition/ChainMetadata.h>
#include <volition/serialization/AbstractSerializable.h>
#include <volition/Singleton.h>
#include <volition/Ledger.h>

namespace Volition {

class AbstractHashable;
class Block;

//================================================================//
// Miner
//================================================================//
class Miner :
    public AbstractSerializable {
protected:

    static constexpr const char* MASTER_BRANCH      = "master";

    string                                          mMinerID;

    CryptoKey                                       mKeyPair;
    list < shared_ptr < AbstractTransaction >>      mPendingTransactions;
    bool                                            mLazy;

    shared_ptr < AbstractPersistenceProvider >      mPersistenceProvider;
    
    shared_ptr < Chain >                            mBestBranch;
    set < shared_ptr < Chain >>                     mBranches;
    
    //----------------------------------------------------------------//
    void                    addTransactions         ( Chain& chain, Block& block );
    void                    extendChain             ( Chain& chain, u64 minTime );
    void                    saveChain               ();
    void                    submitChainRecurse      ( const Chain& chain, size_t blockID );

    //----------------------------------------------------------------//
    void                    AbstractSerializable_serializeFrom      ( const AbstractSerializerFrom& serializer ) override;
    void                    AbstractSerializable_serializeTo        ( AbstractSerializerTo& serializer ) const override;
    virtual time_t          Miner_getTime                           () const;

public:

    enum class SubmissionResponse {
        ACCEPTED = 0,
        RESUBMIT_EARLIER,
    };

    //----------------------------------------------------------------//
    bool                    checkBestBranch         ( string miners ) const;
    size_t                  countBranches           () const;
    void                    extend                  ( bool force = false );
    const Chain*            getBestBranch           () const;
    size_t                  getLongestBranchSize    () const;
    bool                    getLazy                 () const;
    const Ledger&           getLedger               () const;
    string                  getMinerID              () const;
    time_t                  getTime                 () const;
    bool                    hasBranch               ( string miners ) const;
    void                    loadGenesis             ( string path );
    void                    loadKey                 ( string keyfile, string password = "" );
    void                    pushTransaction         ( shared_ptr < AbstractTransaction > transaction );
    void                    setPersistenceProvider  ( shared_ptr < AbstractPersistenceProvider > persistence );
    void                    setGenesis              ( const Block& block );
    void                    setLazy                 ( bool lazy );
    void                    setMinerID              ( string minerID );
                            Miner                   ();
    virtual                 ~Miner                  ();
    void                    selectBranch            ();
    SubmissionResponse      submitBlock             ( const Block& block );
    void                    submitChain             ( const Chain& chain );
};

} // namespace Volition
#endif
