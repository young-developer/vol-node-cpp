// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/AbstractTransactionBody.h>
#include <volition/AccountODBM.h>
#include <volition/Ledger.h>
#include <volition/Miner.h>
#include <volition/Transaction.h>
#include <volition/TransactionContext.h>
#include <volition/TransactionMaker.h>
#include <volition/transactions/Genesis.h>

namespace Volition {

//================================================================//
// Transaction
//================================================================//

//----------------------------------------------------------------//
TransactionResult Transaction::apply ( Ledger& ledger, time_t time, SchemaHandle& schemaHandle ) const {

    try {
        TransactionResult result = this->checkBody ( ledger, time );
        if ( result ) {
            result = this->applyInner ( ledger, schemaHandle, time );
        }
        result.setTransactionDetails ( *this );
        return result;
    }
    catch ( const std::exception &e ) {
        return Format::write ( "Exception occured while applying transaction: %s", e.what ());
    }
    catch ( ... ) {
        return "Exception occured while applying transaction.";
    }
}

//----------------------------------------------------------------//
TransactionResult Transaction::applyInner ( Ledger& ledger, SchemaHandle& schemaHandle, time_t time ) const {
    
    if ( !this->mBody ) return "Missing body.";
    
    TransactionMaker* maker = this->mBody->mMaker.get ();
    if ( !maker ) {
        const Transactions::Genesis* genesis = ledger.isGenesis () ? dynamic_cast < const Transactions::Genesis* >( this->mBody.get ()) : NULL;
        return genesis ? genesis->genesis ( ledger ) : TransactionResult ( "Missing transaction maker." );
    }
    
    AccountODBM accountODBM ( ledger, maker->getAccountName ());
        
    if ( !accountODBM ) return "Transaction maker account not found.";
    
    KeyAndPolicy keyAndPolicy = accountODBM.getKeyAndPolicyOrNull ( maker->getKeyName ());
    if ( !keyAndPolicy ) return "Transaction maker key not found.";
    
    TransactionResult result = this->checkNonceAndSignature ( ledger, accountODBM.mAccountID, keyAndPolicy.mKey );
    if ( result ) {
        
        TransactionContext context ( ledger, schemaHandle, accountODBM, keyAndPolicy, time );
        
        u64 cost = this->mBody->cost ();
        if ( context.mAccount.mBalance < cost ) return "Insufficient funds.";
        
        result = this->mBody->apply ( context );
        
        if ( result ) {
            if ( !ledger.isGenesis ()) {
                
                accountODBM.incAccountTransactionNonce ( this->getNonce (), this->getUUID ());
                
                if ( cost > 0 ) {
                    Account accountUpdated = context.mAccount;
                    accountUpdated.mBalance -= cost;
                    accountODBM.mBody.set ( accountUpdated );
                }
            }
        }
    }
    return result;
}

//----------------------------------------------------------------//
TransactionResult Transaction::checkBody ( Ledger& ledger, time_t time ) const {

    if ( this->mBody->mUUID.size () > MAX_UUID_LENGTH ) return Format::write ( "Transaction UUID exceeds %d-character limit.", MAX_UUID_LENGTH );
    
    if ( this->mBody->mMaxHeight > 0 ) {
        u64 height = ledger.getHeight ();
        if ( height > this->mBody->mMaxHeight ) return Format::write ( "Transaction expired at chain height of %d.", this->mBody->mMaxHeight );
    }
    
    if ( this->mBody->mRecordBy > 0 ) {
        if ( time > this->mBody->mRecordBy ) return Format::write ( "Transaction expired at %s.", (( string )this->mBody->mRecordBy ).c_str ());
    }
    return true;
}

//----------------------------------------------------------------//
bool Transaction::checkMaker ( string accountName, string uuid ) const {

    if ( !this->mBody ) return false;
    if ( this->getUUID ().size () == 0 ) return false;
    TransactionMaker* maker = this->mBody->mMaker.get ();
    return ( maker && ( maker->getAccountName () == accountName ) && ( this->getUUID () == uuid ));
}

//----------------------------------------------------------------//
TransactionResult Transaction::checkNonceAndSignature ( const Ledger& ledger, AccountID accountID, const CryptoKey& key ) const {

    if ( ledger.isGenesis ()) return true;
    if ( this->getUUID ().size () == 0 ) return false;

    TransactionMaker* maker = this->mBody->mMaker.get ();
    Signature* signature = this->mSignature.get ();

    if ( maker && signature ) {
        if ( !this->needsControl ()) {
            u64 nonce = AccountODBM ( ledger, accountID ).mTransactionNonce.get ();
            if ( nonce != this->getNonce ()) return false;
        }
        return key.verify ( *signature, this->mBodyString );
    }
    return false;
}

//----------------------------------------------------------------//
TransactionResult Transaction::control ( Miner& miner ) const {

    if ( !this->needsControl ()) return true;
    if ( !miner.controlPermitted ()) return "CONTROL: Control not permitted by this mining node.";

    Ledger& ledger = miner.getLedger ();

    TransactionMaker* maker = this->mBody->mMaker.get ();
    AccountODBM accountODBM ( ledger, maker->getAccountName ());
    KeyAndPolicy keyAndPolicy = accountODBM.mBody.get ()->getKeyAndPolicy ( maker->getKeyName ());

    TransactionResult result = this->checkNonceAndSignature ( ledger, accountODBM.mAccountID, keyAndPolicy.mKey );
    if ( !result ) return "CONTROL: Invalid account or signature.";
    
    Entitlements entitlements = ledger.getEntitlements < KeyEntitlements >( keyAndPolicy );
    if ( !entitlements.check ( KeyEntitlements::NODE_CONTROL )) return "Permission denied.";
    
    return this->mBody->control ( miner );
}

//----------------------------------------------------------------//
u64 Transaction::getGratuity () const {

    return this->mBody ? this->mBody->gratuity () : 0;
}

//----------------------------------------------------------------//
const TransactionMaker* Transaction::getMaker () const {

    return ( this->mBody && this->mBody->mMaker ) ? this->mBody->mMaker.get () : NULL;
}

//----------------------------------------------------------------//
u64 Transaction::getNonce () const {

    return this->mBody ? this->mBody->nonce () : 0;
}

//----------------------------------------------------------------//
string Transaction::getUUID () const {

    return this->mBody ? this->mBody->uuid () : "";
}

//----------------------------------------------------------------//
u64 Transaction::maturity () const {
    return this->mBody->maturity ();
}

//----------------------------------------------------------------//
bool Transaction::needsControl () const {

    return ( this->mBody && this->mBody->needsControl ());
}

//----------------------------------------------------------------//
void Transaction::setBody ( shared_ptr < AbstractTransactionBody > body ) {

    this->mBody = body;
    this->mBodyString = body ? ToJSONSerializer::toJSONString ( *body ) : "";
}

//----------------------------------------------------------------//
Transaction::Transaction () {
}

//----------------------------------------------------------------//
Transaction::~Transaction () {
}

//----------------------------------------------------------------//
string Transaction::typeString () const {

    return this->mBody ? this->mBody->typeString () : "";
}

//----------------------------------------------------------------//
u64 Transaction::weight () const {
    return this->mBody->weight ();
}

//================================================================//
// overrides
//================================================================//

//----------------------------------------------------------------//
void Transaction::AbstractSerializable_serializeFrom ( const AbstractSerializerFrom& serializer ) {

    serializer.serialize ( "signature", this->mSignature );
    
    if ( serializer.has ( "bodyIn" )) {
        TransactionBodyPtr body;
        serializer.serialize ( "bodyIn", body );
        this->setBody ( body );
    }
    else {
        serializer.serialize ( "body", this->mBodyString );
        if ( this->mBodyString.size ()) {
        
            FromJSONSerializer::fromJSONString ( this->mBody, this->mBodyString );
            assert ( this->mBody );
        }
    }
}

//----------------------------------------------------------------//
void Transaction::AbstractSerializable_serializeTo ( AbstractSerializerTo& serializer ) const {

    serializer.serialize ( "signature",     this->mSignature );
    serializer.serialize ( "body",          this->mBodyString );
}

} // namespace Volition
