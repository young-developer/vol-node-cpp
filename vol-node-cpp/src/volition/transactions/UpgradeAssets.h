// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_TRANSACTIONS_UPGRADE_ASSETS_H
#define VOLITION_TRANSACTIONS_UPGRADE_ASSETS_H

#include <volition/common.h>
#include <volition/Format.h>
#include <volition/AbstractTransactionBody.h>
#include <volition/Policy.h>

namespace Volition {
namespace Transactions {

//================================================================//
// UpgradeAssets
//================================================================//
class UpgradeAssets :
    public AbstractTransactionBody {
public:

    TRANSACTION_TYPE ( "UPGRADE_ASSETS" )
    TRANSACTION_WEIGHT ( 1 )
    TRANSACTION_MATURITY ( 0 )

    SerializableMap < string, string >      mUpgrades;

    //----------------------------------------------------------------//
    void AbstractSerializable_serializeFrom ( const AbstractSerializerFrom& serializer ) override {
        AbstractTransactionBody::AbstractSerializable_serializeFrom ( serializer );
        
        serializer.serialize ( "upgrades",          this->mUpgrades );
    }
    
    //----------------------------------------------------------------//
    void AbstractSerializable_serializeTo ( AbstractSerializerTo& serializer ) const override {
        AbstractTransactionBody::AbstractSerializable_serializeTo ( serializer );
        
        serializer.serialize ( "upgrades",          this->mUpgrades );
    }

    //----------------------------------------------------------------//
    TransactionResult AbstractTransactionBody_apply ( TransactionContext& context ) const override {

        if ( !context.mKeyEntitlements.check ( KeyEntitlements::UPGRADE_ASSETS )) return "Permission denied.";

        Ledger& ledger = context.mLedger;
        const Account& account = context.mAccount;
        const Schema& schema = *context.mSchemaHandle;

        Account::Index accountIndex = account.mIndex;

        // check the upgrades
        SerializableMap < string, string >::const_iterator upgradeIt = this->mUpgrades.cbegin ();
        for ( ; upgradeIt != this->mUpgrades.end (); ++upgradeIt ) {
            
            string assetID = upgradeIt->first;
            string upgradeType = upgradeIt->second;
            
            AssetODBM assetODBM ( ledger, AssetID::decode ( assetID ));

            if ( !assetODBM.mOwner.exists ()) return Format::write ( "Asset %s does not exist.", assetID.c_str ());
            if ( assetODBM.mOwner.get () != accountIndex ) return Format::write ( "Asset %s does not belong to account %s.", assetID.c_str (), account.mName.c_str ());
            if ( !schema.canUpgrade ( assetODBM.mType.get (), upgradeType )) return Format::write (  "Cannot upgrade asset %s to %s.",  assetID.c_str (),  upgradeType.c_str ());
        }
        
        // perform the upgrades
        upgradeIt = this->mUpgrades.cbegin ();
        for ( ; upgradeIt != this->mUpgrades.end (); ++upgradeIt ) {
            
            AssetODBM assetODBM ( ledger, AssetID::decode ( upgradeIt->first ) );
            assetODBM.mType.set ( upgradeIt->second );
        }
        return true;
    }
};

} // namespace Transactions
} // namespace Volition
#endif
