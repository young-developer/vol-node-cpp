// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_ABSTRACTTRANSACTIONBODY_H
#define VOLITION_ABSTRACTTRANSACTIONBODY_H

#include <volition/common.h>
#include <volition/CryptoKey.h>
#include <volition/Digest.h>
#include <volition/Ledger.h>
#include <volition/Miner.h>
#include <volition/serialization/Serialization.h>
#include <volition/TransactionContext.h>
#include <volition/TransactionMaker.h>
#include <volition/TransactionResult.h>

namespace Volition {

#define TRANSACTION_TYPE(typeString)                                \
    static constexpr const char* TYPE_STRING = typeString;          \
    string AbstractTransactionBody_typeString () const override {   \
        return TYPE_STRING;                                         \
    }

#define TRANSACTION_WEIGHT(weight)                                  \
    static constexpr u64 WEIGHT = weight;                           \
    u64 AbstractTransactionBody_weight () const override {          \
        return WEIGHT;                                              \
    }

#define TRANSACTION_MATURITY(maturity)                              \
    static constexpr u64 MATURITY = maturity;                       \
    u64 AbstractTransactionBody_maturity () const override {        \
        return MATURITY;                                            \
    }

class Miner;
class Transaction;
class TransactionContext;

typedef shared_ptr < AbstractSerializable > TransactionDetailsPtr;

//================================================================//
// AbstractTransactionBody
//================================================================//
class AbstractTransactionBody :
    public virtual AbstractSerializable {
protected:

    friend class Transaction;

    SerializableUniquePtr < TransactionMaker >      mMaker;
    u64                                             mMaxHeight; // expiration block
    SerializableTime                                mRecordBy; // expiration date/time
    string                                          mUUID;

    //----------------------------------------------------------------//
    void                    AbstractSerializable_serializeFrom      ( const AbstractSerializerFrom& serializer ) override;
    void                    AbstractSerializable_serializeTo        ( AbstractSerializerTo& serializer ) const override;

    //----------------------------------------------------------------//
    virtual TransactionResult       AbstractTransactionBody_apply           ( TransactionContext& context ) const = 0;
    virtual TransactionResult       AbstractTransactionBody_genesis         ( AbstractLedger& ledger ) const;
    virtual TransactionDetailsPtr   AbstractTransactionBody_getDetails      ( const AbstractLedger& ledger ) const;
    virtual u64                     AbstractTransactionBody_getVOL          ( const TransactionContext& context ) const;
    virtual u64                     AbstractTransactionBody_maturity        () const = 0;
    virtual string                  AbstractTransactionBody_typeString      () const = 0;
    virtual u64                     AbstractTransactionBody_weight          () const = 0;

public:

    GET_COMPOSED ( u64,         Fees,               this->mMaker,      0 )
    GET_COMPOSED ( u64,         Gratuity,           this->mMaker,      0 )
    GET_COMPOSED ( u64,         Nonce,              this->mMaker,      0 )
    GET_COMPOSED ( u64,         ProfitShare,        this->mMaker,      0 )
    GET_COMPOSED ( u64,         TransferTax,        this->mMaker,      0 )

    GET ( u64,                  Maturity,           this->AbstractTransactionBody_maturity ())
    GET ( string,               TypeString,         this->AbstractTransactionBody_typeString ())
    GET ( u64,                  Weight,             this->AbstractTransactionBody_weight ())
    
    GET_SET ( u64,              MaxHeight,          this->mMaxHeight )
    GET_SET ( time_t,           RecordBy,           this->mRecordBy )
    GET_SET ( string,           UUID,               this->mUUID )

    //----------------------------------------------------------------//
                                AbstractTransactionBody                 ();
                                ~AbstractTransactionBody                ();
    TransactionResult           apply                                   ( TransactionContext& context ) const;
    TransactionResult           genesis                                 ( AbstractLedger& ledger );
    TransactionDetailsPtr       getDetails                              ( const AbstractLedger& ledger ) const;
    const TransactionMaker*     getMaker                                () const;
    u64                         getVOL                                  ( const TransactionContext& context ) const;
    void                        setMaker                                ( const TransactionMaker& maker );
};

} // namespace Volition
#endif
