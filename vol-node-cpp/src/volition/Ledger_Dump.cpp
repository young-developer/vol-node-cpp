// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/AccountODBM.h>
#include <volition/Asset.h>
#include <volition/AssetMethod.h>
#include <volition/AssetMethodInvocation.h>
#include <volition/AssetODBM.h>
#include <volition/Block.h>
#include <volition/Format.h>
#include <volition/InventoryLogEntry.h>
#include <volition/Ledger.h>
#include <volition/Ledger_Dump.h>
#include <volition/LedgerFieldODBM.h>
#include <volition/LuaContext.h>
#include <volition/SQLite.h>
#include <volition/TransactionMaker.h>

namespace Volition {

//================================================================//
// Ledger_Dump
//================================================================//

//----------------------------------------------------------------//
void Ledger_Dump::dump ( string filename ) {

    SQLite db ( filename );
    assert ( db );
    
    db.exec ( "DROP TABLE IF EXISTS accounts" );
    db.exec ( "DROP TABLE IF EXISTS keys" );
    db.exec ( "DROP TABLE IF EXISTS assets" );
    
    db.exec ( "CREATE TABLE accounts ( id INTEGER PRIMARY KEY, name TEXT NOT NULL, balance INTEGER, policy TEXT NOT NULL, bequest TEXT NOT NULL )" );
    db.exec ( "CREATE TABLE keys ( id INTEGER PRIMARY KEY, ownerID INTEGER, name TEXT NOT NULL, body TEXT NOT NULL, policy TEXT NOT NULL, bequest TEXT NOT NULL )" );
    db.exec ( "CREATE TABLE assets ( id INTEGER PRIMARY KEY, ownerID INTEGER, body TEXT NOT NULL )" );
    
    s64 keyCount = 0;
    
    Ledger& ledger = this->getLedger ();
    
    AccountID::Index totalAccounts = ledger.getValue < AccountID::Index >( Ledger::keyFor_globalAccountCount ());
    for ( AccountID::Index i = 0; i < totalAccounts; ++i ) {
        
        AccountODBM accountODBM ( ledger, AccountID ( i ));
        if ( !accountODBM ) continue;
    
        shared_ptr < const Account > account = accountODBM.mBody.get ( );
        string name = accountODBM.mName.get ( "" );
        
        db.exec (
            Format::write ( "REPLACE INTO accounts ( id, name, balance, policy, bequest ) VALUES ( ?1, ?2, ?3, ?4, ?5 )" ),
            [ &accountODBM, &account, name ]( sqlite3_stmt* stmt ) {
                
                string policy   = ToJSONSerializer::toJSONString ( account->mPolicy );
                string bequest  = account->mBequest ? ToJSONSerializer::toJSONString ( *account->mBequest ) : "";
            
                sqlite3_bind_int64  ( stmt, 1, ( s64 )accountODBM.mAccountID.mIndex );
                sqlite3_bind_text   ( stmt, 2, name.c_str (), ( int )name.size (), SQLITE_TRANSIENT );
                sqlite3_bind_int64  ( stmt, 3, ( s64 )account->mBalance );
                sqlite3_bind_text   ( stmt, 4, policy.c_str (), ( int )policy.size (), SQLITE_TRANSIENT );
                sqlite3_bind_text   ( stmt, 5, bequest.c_str (), ( int )bequest.size (), SQLITE_TRANSIENT );
            }
        );
        
        SerializableMap < string, KeyAndPolicy >::const_iterator keyAndPolicyIt = account->mKeys.cbegin ();
        for ( ; keyAndPolicyIt != account->mKeys.cend (); ++keyAndPolicyIt ) {
            
            string keyName = keyAndPolicyIt->first;
            const KeyAndPolicy& keyAndPolicy = keyAndPolicyIt->second;
            
            db.exec (
                Format::write ( "REPLACE INTO keys ( id, ownerID, name, body, policy, bequest ) VALUES ( ?1, ?2, ?3, ?4, ?5, ?6 )" ),
                [ &accountODBM, &keyAndPolicy, &keyCount, keyName ]( sqlite3_stmt* stmt ) {
                    
                    string body     = ToJSONSerializer::toJSONString ( keyAndPolicy.mKey );
                    string policy   = ToJSONSerializer::toJSONString ( keyAndPolicy.mPolicy );
                    string bequest  = keyAndPolicy.mBequest ? ToJSONSerializer::toJSONString ( *keyAndPolicy.mBequest ) : "";
                
                    sqlite3_bind_int64  ( stmt, 1, ( s64 )keyCount++ );
                    sqlite3_bind_int64  ( stmt, 2, ( s64 )accountODBM.mAccountID.mIndex );
                    sqlite3_bind_text   ( stmt, 3, keyName.c_str (), ( int )keyName.size (), SQLITE_TRANSIENT );
                    sqlite3_bind_text   ( stmt, 4, body.c_str (), ( int )body.size (), SQLITE_TRANSIENT );
                    sqlite3_bind_text   ( stmt, 5, policy.c_str (), ( int )policy.size (), SQLITE_TRANSIENT );
                    sqlite3_bind_text   ( stmt, 6, bequest.c_str (), ( int )bequest.size (), SQLITE_TRANSIENT );
                }
            );
        }
    }
    
    AssetID::Index totalAssets = ledger.getValue < AssetID::Index >( Ledger::keyFor_globalAssetCount ());
    for ( AssetID::Index i = 0; i < totalAssets; ++i ) {
    
        AssetODBM assetODBM ( ledger, AssetID ( i ));
        if ( !assetODBM ) continue;
    
        shared_ptr < const Asset > asset = assetODBM.getAsset ( ledger.getSchema (), true );
        
        db.exec (
            Format::write ( "REPLACE INTO assets ( id, ownerID, body ) VALUES ( ?1, ?2, ?3 )" ),
            [ &ledger, &assetODBM, &asset ]( sqlite3_stmt* stmt ) {
                
                string body = ToJSONSerializer::toJSONString ( *asset );
                AccountID::Index accountIndex = ledger.getAccountID ( asset->mOwner );
                
                sqlite3_bind_int64  ( stmt, 1, ( s64 )asset->mAssetID.mIndex );
                sqlite3_bind_int64  ( stmt, 2, ( s64 )accountIndex);
                sqlite3_bind_text   ( stmt, 3, body.c_str (), ( int )body.size (), SQLITE_TRANSIENT ) ;
            }
        );
    }
}

} // namespace Volition
