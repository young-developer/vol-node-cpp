/* eslint-disable no-whitespace-before-property */
/* eslint-disable no-loop-func */

import { assert, crypto, excel, hooks, randomBytes, RevocableContext, SingleColumnContainerView, storage, StorageContext, util } from 'fgc';
import * as bcrypt          from 'bcryptjs';
import _                    from 'lodash';
import { action, computed, extendObservable, observable, observe, runInAction } from 'mobx';
import React                from 'react';

const STORE_ACCOUNTS            = '.vol_accounts';
const STORE_NETWORKS            = '.vol_networks';
const STORE_NODE                = '.vol_node';
const STORE_NODES               = '.vol_nodes';
const STORE_PASSWORD_HASH       = '.vol_password_hash';
const STORE_PENDING_ACCOUNTS    = '.vol_pending_accounts';
const STORE_SESSION             = '.vol_session';

export const NODE_TYPE = {
    UNKNOWN:    'UNKNOWN',
    MINING:     'MINING',
    MARKET:     'MARKET',
};

export const NODE_STATUS = {
    UNKNOWN:    'UNKNOWN',
    ONLINE:     'ONLINE',
    OFFLINE:    'OFFLINE',
};

//================================================================//
// AppStateService
//================================================================//
export class AppStateService {

    //----------------------------------------------------------------//
    @action
    affirmAccountAndKey ( password, accountID, keyName, phraseOrKey, privateKeyHex, publicKeyHex ) {

        console.log ( 'AFFIRMING ACCOUNT AND KEY!' );

        this.assertHasNetwork ();
        this.assertPassword ( password );

        const accounts = this.network.accounts;

        let account = accounts [ accountID ] || {
            keys: {},
            pendingTransactions: [],
            stagedTransactions: [],
        };

        let key = account.keys [ keyName ] || {};

        key.phraseOrKeyAES      = crypto.aesPlainToCipher ( phraseOrKey, password );
        key.privateKeyHexAES    = crypto.aesPlainToCipher ( privateKeyHex, password );
        key.publicKeyHex        = publicKeyHex;

        account.keys [ keyName ] = key;

        accounts [ accountID ] = account;
    }

    //----------------------------------------------------------------//
    @action
    affirmNetwork ( name, nodeURL ) {

        if ( !_.has ( this.networks, name )) {
            this.networks [ name ] = {
                nodeURL:    nodeURL,
                accounts:   {},
            };
        }
        else {
            this.networks [ name ].nodeUTL = nodeURL;
        }
    }

    //----------------------------------------------------------------//
    assertHasAccount () {
        this.assertHasNetwork ();
        if ( !this.hasAccount ) throw new Error ( 'No account selected.' );
    }

    //----------------------------------------------------------------//
    assertHasNetwork () {
        if ( !this.hasNetwork ) throw new Error ( 'No network selected.' );
    }

    //----------------------------------------------------------------//
    assertPassword ( password ) {
        if ( !this.checkPassword ( password )) throw new Error ( 'Invalid wallet password.' );
    }

    //----------------------------------------------------------------//
    checkPassword ( password ) {
        const passwordHash = ( this.passwordHash ) || '';
        return (( passwordHash.length > 0 ) && bcrypt.compareSync ( password, passwordHash ));
    }

    //----------------------------------------------------------------//
    @action
    clearPendingTransactions () {

        if ( this.hasAccount ) {
            this.account.pendingTransactions = [];
        }
    }

    //----------------------------------------------------------------//
    @action
    clearStagedTransactions () {

        if ( this.hasAccount ) {
            this.account.stagedTransactions = [];
        }
    }

    //----------------------------------------------------------------//
    @action
    confirmTransactions ( nonce ) {

        if ( this.hasAccount ) {
            let pendingTransactions = this.account.pendingTransactions;
            while (( pendingTransactions.length > 0 ) && ( pendingTransactions [ 0 ].nonce <= nonce )) {
                pendingTransactions.shift ();
            }
        }
    }

    //----------------------------------------------------------------//
    constructor ( networkID, accountID ) {

        this.revocable      = new RevocableContext ();

        networkID = networkID || '';
        accountID = accountID || '';

        extendObservable ( this, {
            networkID:              '',
            accountID:              '',
            keyName:                '',
            accountInfo:            null,
            nextTransactionCost:    0,
        });

        const storageContext = new StorageContext ();

        storageContext.persist ( this, 'networks',          STORE_NETWORKS,             {}); // account names index by network name
        storageContext.persist ( this, 'passwordHash',      STORE_PASSWORD_HASH,        '' );
        storageContext.persist ( this, 'pendingAccounts',   STORE_PENDING_ACCOUNTS,     {});
        storageContext.persist ( this, 'session',           STORE_SESSION,              this.makeSession ( false ));

        this.storage = storageContext;

        runInAction (() => {

            if ( _.has ( this.networks, networkID )) {

                this.networkID = networkID;
                const network = this.network;

                if ( _.has ( network.accounts, accountID )) {
                    this.accountID = accountID;
                    this.keyName = this.getDefaultAccountKeyName ();
                }
            }
        });

        this.setAccountInfo ();
    }

    //----------------------------------------------------------------//
    @action
    deleteAccount ( accountID ) {

        this.assertHasNetwork ();

        accountID = accountID || this.accountID;

        if ( accountID in this.network.accounts ) {
            delete this.network.accounts [ accountID ];
        }
        
        if ( accountID === this.accountID ) {
            this.accountID = '';
            this.setAccountInfo ();
        }
    }

    //----------------------------------------------------------------//
    @action
    deleteAccountRequest ( requestID ) {

        delete this.pendingAccounts [ requestID ];
    }

    //----------------------------------------------------------------//
    @action
    deleteNetwork ( networkName ) {

        delete this.networks [ networkName ];
    }

    //----------------------------------------------------------------//
    @action
    deleteStorage () {

        storage.clear ();
        this.networkID = '';
        this.accountID = '';
        this.keyName = '';
        this.nextTransactionCost = 0;
        this.setAccountInfo ();
    }

    //----------------------------------------------------------------//
    @action
    deleteTransactions () {

        if ( this.hasAccount ) {
            this.account.pendingTransactions = [];
            this.account.stagedTransactions = [];
        }
    }

    //----------------------------------------------------------------//
    finalize () {

        this.revocable.finalize ();
    }

    //----------------------------------------------------------------//
    findAccountIdByPublicKey ( publicKey ) {

        if ( this.hasNetwork ) {
            const accounts = this.network.accounts;
            for ( let accountID in accounts ) {
                const account = accounts [ accountID ];
                for ( let keyName in account.keys ) {
                    const key = account.keys [ keyName ];
                    if ( key.publicKey === publicKey ) return accountID;
                }
            }
        }
        return false;
    }

    //----------------------------------------------------------------//
    @computed
    get account () {
        return this.getAccount ();
    }

    //----------------------------------------------------------------//
    getAccount ( accountID ) {

        if ( this.hasNetwork ) {
            accountID = accountID || this.accountID;
            const accounts = this.network.accounts;
            return _.has ( accounts, accountID ) ? accounts [ accountID ] : false;
        }
        return false;
    }

    //----------------------------------------------------------------//
    @computed
    get accountKeyNames () {
        const account = this.account;
        return ( account && Object.keys ( account.keys )) || [];
    }

    //----------------------------------------------------------------//
    @computed
    get assetsUtilized () {

        let assetsUtilized = [];

        const pendingTransactions = this.pendingTransactions;
        for ( let i in pendingTransactions ) {
            assetsUtilized = assetsUtilized.concat ( pendingTransactions [ i ].assets );
        }

        const stagedTransactions = this.stagedTransactions;
        for ( let i in stagedTransactions ) {
            assetsUtilized = assetsUtilized.concat ( stagedTransactions [ i ].assets );
        }
        return assetsUtilized;
    }

    //----------------------------------------------------------------//
    @computed
    get balance () {

        let cost = 0;

        const pendingTransactions = this.pendingTransactions;
        for ( let i in pendingTransactions ) {
            cost += pendingTransactions [ i ].cost;
        }

        const stagedTransactions = this.stagedTransactions;
        for ( let i in stagedTransactions ) {
            cost += stagedTransactions [ i ].cost;
        }

        return this.accountInfo.balance - cost - this.nextTransactionCost;
    }

    //----------------------------------------------------------------//
    @computed
    get canSubmitTransactions () {

        if ( !this.hasAccount ) return false;
        if ( this.nextNonce < 0 ) return false;
        if ( this.account.stagedTransactions.length === 0 ) return false;

        return true;
    }

    //----------------------------------------------------------------//
    getDefaultAccountKeyName () {

        const defaultKeyName = 'master';
        const accountKeyNames = this.accountKeyNames;
        if ( accountKeyNames.includes ( defaultKeyName )) return defaultKeyName;
        return (( accountKeyNames.length > 0 ) && accountKeyNames [ 0 ]) || '';
    }

    //----------------------------------------------------------------//
    @computed
    get hasAccount () {
        return ( this.accountID && this.account );
    }

    //----------------------------------------------------------------//
    @computed
    get hasAccountInfo () {
        return ( this.accountInfo.nonce >= 0 );
    }

    //----------------------------------------------------------------//
    @computed
    get hasNetwork () {
        return ( this.networkID && this.network );
    }

    //----------------------------------------------------------------//
    @computed
    get key () {
        const account = this.getAccount ();
        return account ? account.keys [ this.keyName ] : null;
    }

    //----------------------------------------------------------------//
    @computed
    get nextNonce () {

        if ( this.nonce < 0 ) return -1;

        const pendingTransactions = this.pendingTransactions;
        const pendingTop = pendingTransactions.length;

        return pendingTop > 0 ? pendingTransactions [ pendingTop - 1 ].nonce + 1 : this.nonce;
    }

    //----------------------------------------------------------------//
    @computed
    get network () {
        return this.getNetwork ();
    }

    //----------------------------------------------------------------//
    getNetwork ( networkID ) {
        networkID = networkID || this.networkID;
        const networks = this.networks;
        return _.has ( networks, networkID ) ? networks [ networkID ] : null;
    }

    //----------------------------------------------------------------//
    @computed
    get nonce () {
        return this.accountInfo.nonce;
    }

    //----------------------------------------------------------------//
    @computed
    get pendingTransactions () {
        return this.account.pendingTransactions || [];
    }


    //----------------------------------------------------------------//
    @computed
    get stagedTransactions () {
        return this.account.stagedTransactions || [];
    }

    //----------------------------------------------------------------//
    hasUser () {
        return ( this.passwordHash.length > 0 );
    }

    //----------------------------------------------------------------//
    @action
    importAccountRequest ( requestID, password ) {

        // if ( !this.checkPassword ( password )) throw new Error ( 'Invalid wallet password' );

        // const pending = this.pendingAccounts [ requestID ];
        // if ( !pending ) throw new Error ( 'Account request not found' );

        // const phraseOrKey       = crypto.aesCipherToPlain ( pending.phraseOrKeyAES, password );
        // const privateKeyHex     = crypto.aesCipherToPlain ( pending.privateKeyHexAES, password );

        // this.affirmAccountAndKey (
        //     password,
        //     pending.accountID,
        //     pending.keyName,
        //     phraseOrKey,
        //     privateKeyHex,
        //     pending.publicKeyHex,
        // );

        // delete this.pendingAccounts [ requestID ];
    }

    //----------------------------------------------------------------//
    isLoggedIn () {
        return ( this.session.isLoggedIn === true );
    }

    //----------------------------------------------------------------//
    @action
    login ( password ) {

        this.session = this.makeSession ( this.checkPassword ( password ));
    }

    //----------------------------------------------------------------//
    makeSession ( isLoggedIn ) {
        return { isLoggedIn: isLoggedIn };
    }

    //----------------------------------------------------------------//
    @action
    pushTransaction ( transaction ) {

        this.assertHasAccount ();

        let account = this.account;

        let memo = {
            type:               transaction.type,
            note:               transaction.note,
            cost:               transaction.getCost (),
            body:               JSON.stringify ( transaction.body, null, 4 ),
            assets:             transaction.assetsUtilized,
        }

        this.account.stagedTransactions.push ( memo );
        this.setNextTransactionCost ( 0 );
    }

    //----------------------------------------------------------------//
    @action
    register ( passwordHash ) {

        this.session = this.makeSession ( true );
        this.passwordHash = passwordHash;
    }

    //----------------------------------------------------------------//
    @action
    renameAccount ( oldName, newName ) {

        // if ( !_.has ( this.accounts, oldName )) return;        
        
        // this.accounts [ newName ] = _.cloneDeep ( this.accounts [ oldName ]); // or mobx will bitch at us
        // delete this.accounts [ oldName ];

        // if ( this.accountID === oldName ) {
        //     this.setAccount ( newName );
        // }
    }

    //----------------------------------------------------------------//
    @action
    selectKey ( keyName ) {

        this.assertHasAccount ();

        if (( this.keyName !== keyName ) && this.accountKeyNames.includes ( keyName )) {
            this.keyName = keyName;
        }
    }

    //----------------------------------------------------------------//
    @action
    setAccountInfo ( balance, nonce ) {

        this.accountInfo = {
            balance:    typeof ( balance ) === 'number' ? balance : -1,
            nonce:      typeof ( nonce ) === 'number' ? nonce : -1,
        };
    }

    //----------------------------------------------------------------//
    @action
    setAccountRequest ( password, phraseOrKey, keyID, privateKeyHex, publicKeyHex ) {

        if ( !this.checkPassword ( password )) throw new Error ( 'Invalid wallet password' );

        const phraseOrKeyAES = crypto.aesPlainToCipher ( phraseOrKey, password );
        if ( phraseOrKey !== crypto.aesCipherToPlain ( phraseOrKeyAES, password )) throw new Error ( 'AES error' );

        const privateKeyHexAES = crypto.aesPlainToCipher ( privateKeyHex, password );
        if ( privateKeyHex !== crypto.aesCipherToPlain ( privateKeyHexAES, password )) throw new Error ( 'AES error' );

        let requestID;
        do {
            requestID = `vol_${ randomBytes ( 6 ).toString ( 'hex' )}`;
        } while ( _.has ( this.pendingAccounts, requestID ));

        const request = {
            key: {
                type:               'EC_HEX',
                groupName:          'secp256k1',
                publicKey:          publicKeyHex,
            },
        }

        const requestJSON   = JSON.stringify ( request );
        const encoded       = Buffer.from ( requestJSON, 'utf8' ).toString ( 'base64' );

        const pendingAccount = {
            requestID:              requestID,
            encoded:                encoded,
            keyID:                  keyID, // needed to recover account later
            publicKeyHex:           publicKeyHex,
            privateKeyHexAES:       privateKeyHexAES,
            phraseOrKeyAES:         phraseOrKeyAES,
            readyToImport:          false,
        }

        this.pendingAccounts [ requestID ] = pendingAccount;
    }

    //----------------------------------------------------------------//
    @action
    setNextTransactionCost ( cost ) {

        this.nextTransactionCost = cost || 0;
    }

    //----------------------------------------------------------------//
    @action
    async submitTransactions ( password ) {

        this.assertHasAccount ();
        this.assertPassword ( password );

        let stagedTransactions      = this.account.stagedTransactions;
        let pendingTransactions     = this.account.pendingTransactions;

        try {

            while ( this.canSubmitTransactions ) {

                let memo = stagedTransactions [ 0 ];
                let nonce = this.nextNonce;

                let body = JSON.parse ( memo.body );
                body.maker.nonce = nonce;

                let envelope = {
                    body: JSON.stringify ( body, null, 4 ),
                };

                const hexKey            = this.account.keys [ body.maker.keyName ];
                const privateKeyHex     = crypto.aesCipherToPlain ( hexKey.privateKeyHexAES, password );
                const key               = await crypto.keyFromPrivateHex ( privateKeyHex );

                envelope.signature = {
                    hashAlgorithm:  'SHA256',
                    digest:         key.hash ( envelope.body ),
                    signature:      key.sign ( envelope.body ),
                };
                
                await this.revocable.fetch ( `${ this.network.nodeURL }/transactions`, {
                    method :    'POST',
                    headers :   { 'content-type': 'application/json' },
                    body :      JSON.stringify ( envelope, null, 4 ),
                });

                runInAction (() => {
                    stagedTransactions.shift ();
                    pendingTransactions.push ({
                        type:                       memo.type,
                        note:                       memo.note,
                        cost:                       memo.cost,
                        body:                       envelope,
                        nonce:                      nonce,
                        assets:                     memo.assets,
                    });
                });
            }
        }
        catch ( error ) {
             console.log ( 'AN ERROR!', error );
        }
    }

    //----------------------------------------------------------------//
    @action
    updateAccount ( accountUpdate, entitlements ) {

        let account = this.account;
        if ( !account ) return;

        account.policy          = accountUpdate.policy;
        account.bequest         = accountUpdate.bequest;
        account.entitlements    = entitlements.account;

        for ( let keyName in accountUpdate.keys ) {

            let key = account.keys [ keyName ];
            if ( !key ) continue;

            let keyUpdate = accountUpdate.keys [ keyName ];
            let publicKeyHex = keyUpdate.key.publicKey;

            // TODO: handle all the business around expiring keys
            if ( key.publicKeyHex === keyUpdate.key.publicKey ) {
                key.policy          = keyUpdate.policy;
                key.bequest         = keyUpdate.bequest;
                key.entitlements    = entitlements.keys [ keyName ];
            }
        }
    }

    //----------------------------------------------------------------//
    @action
    updateAccountRequest ( requestID, accountName, keyName ) {

        let pendingAccount = this.pendingAccounts [ requestID ];
        pendingAccount.accountID = accountName;
        pendingAccount.keyName = keyName;
        pendingAccount.readyToImport = true;
    }
}
