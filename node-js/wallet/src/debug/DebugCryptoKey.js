/* eslint-disable no-whitespace-before-property */
/* eslint-disable no-loop-func */

import { Service, useService }                                                      from '../Service';
import * as crypto                                                                  from '../util/crypto';
import * as util                                                                    from '../util/util';
import { action, computed, extendObservable, observable, observe, runInAction }     from 'mobx';
import { observer }                                                                 from 'mobx-react';
import React, { useState }                                                          from 'react';
import { Button, Divider, Dropdown, Form, Grid, Header, Icon, Modal, Segment }      from 'semantic-ui-react';

const DEFAULT_KEY = `-----BEGIN PRIVATE KEY-----
MIGEAgEAMBAGByqGSM49AgEGBSuBBAAKBG0wawIBAQQg6D1bbfkvkOZphXe8doC5
WxxaY/M+CZAKGTlz8yOmgiahRANCAARt6hL7ZOgrkL3bAqeS5IjiAoV9cN2ptAiQ
LnTu3TW13HIuhqXiFIo4JyBql4xVPx30GQQMORjK7aI598njQruR
-----END PRIVATE KEY-----`;

const DEFAULT_URL = 'http://localhost:9090/test/signature';

//================================================================//
// DebugCryptoKeyController
//================================================================//
class DebugCryptoKeyController extends Service {

    @observable phraseOrKey     = '';
    @observable message         = '';
    @observable signature       = '';
    @observable url             = DEFAULT_URL;

    @observable keyError        = false;
    @observable sigError        = false;

    //----------------------------------------------------------------//
    constructor ( appState ) {
        super ();
        this.setPhraseOrKey ( DEFAULT_KEY );
    }

    //----------------------------------------------------------------//
    @action
    async getKeyAsync () {

        try {
            const key = await crypto.loadKeyAsync ( this.phraseOrKey );
            
            console.log ( 'KEY_ID',         key.getKeyID ());
            console.log ( 'PUBLIC_KEY',     key.getPublicHex ());
            console.log ( 'PRIVATE_KEY',    key.getPrivateHex ());

            return key;
        }
        catch ( error ) {
            console.log ( error );
            runInAction (() => { this.keyError = true });
        }
        return false;
    }

    //----------------------------------------------------------------//
    async post () {

        const envelope = {
            key: {
                type:           'EC_HEX',
                groupName:      'secp256k1',
                privateKey:     this.key.getPrivateHex (),
                publicKey:      this.key.getPublicHex (),
            },
            signature: {
                hashAlgorithm:  'SHA256',
                digest:         this.key.hash ( this.message ),
                signature:      this.signature,
            },
            message:            this.message,
        };

        try {
            await this.revocableFetch ( this.url, {
                method : 'POST',
                headers : { 'content-type': 'application/json' },
                body : JSON.stringify ( envelope )
            });
        }
        catch ( error ) {
            console.log ( error );
        }
    }

    //----------------------------------------------------------------//
    @action
    setMessage ( message ) {
        this.message = message;
        this.verify ();
    }

    //----------------------------------------------------------------//
    @action
    async setPhraseOrKey ( phraseOrKey ) {

        this.phraseOrKey = phraseOrKey;
        this.signature = '';
        this.keyError = false;
        this.sigError = false;

        this.key = false;

        try {
            const key = await crypto.loadKeyAsync ( this.phraseOrKey );
            
            console.log ( 'KEY_ID',         key.getKeyID ());
            console.log ( 'PUBLIC_KEY',     key.getPublicHex ());
            console.log ( 'PRIVATE_KEY',    key.getPrivateHex ());

            runInAction (() => { this.key = key });
        }
        catch ( error ) {
            runInAction (() => { this.keyError = true });
        }
    }

    //----------------------------------------------------------------//
    @action
    setSignature ( signature ) {
        this.signature = signature;
        this.verify ();
    }

    //----------------------------------------------------------------//
    @action
    setURL ( url ) {
        this.url = url;
    }

    //----------------------------------------------------------------//
    @action
    sign () {

        this.signature = '';
        this.sigError = false;

        if ( this.key ) {
            this.signature = this.key.sign ( this.message );
        }
    }

    //----------------------------------------------------------------//
    @action
    async verify () {

        this.sigError = false;

        if ( this.signature.length === 0 ) return;

        if ( this.key ) {
            try {
                this.sigError = !this.key.verify ( this.message, this.signature );
            }
            catch ( error ) {
                console.log ( error );
                this.sigError = true;
            }
        }
    }
}

//================================================================//
// DebugCryptoKey
//================================================================//
const DebugCryptoKey = observer (( props ) => {

    const controller    = useService (() => new DebugCryptoKeyController ());

    return (
    
        <div className='login-form'>
            {/*
                The styles below are necessary for the correct render of this form.
                You can do same with CSS, the main idea is that all the elements up to the `Grid`
                below must have a height of 100%.
            */}
            <style>{`
                body > div,
                body > div > div,
                body > div > div > div.login-form {
                    height: 100%;
                }
            `}</style>
            <Grid textAlign = "center" style = {{ height: '100%' }} verticalAlign = "middle">
                <Grid.Column style={{ maxWidth: 450 }}>
                <Header as="h2" color="teal" textAlign="center">
                    Test Mnemonic Phrase or Private Key
                </Header>
                <Form size = "large">
                    <Segment stacked>
                        <Form.TextArea
                            rows = { 8 }
                            placeholder = "Mnemonic Phrase or Private Key"
                            name = "phraseOrKey"
                            value = { controller.phraseOrKey }
                            onChange = {( event ) => { controller.setPhraseOrKey ( event.target.value )}}
                            error = { controller.keyError }
                        />
                        <Form.TextArea
                            placeholder = "Message to Sign"
                            name = "message"
                            value = { controller.message }
                            onChange = {( event ) => { controller.setMessage ( event.target.value )}}
                        />
                        <Form.TextArea
                            placeholder = "Signature"
                            name = "signature"
                            value = { controller.signature }
                            onChange = {( event ) => { controller.setSignature ( event.target.value )}}
                            error = { controller.sigError }
                        />
                        <Button
                            color = "teal"
                            disabled = { controller.keyError }
                            fluid size = "large"
                            onClick = {() => { controller.sign ()}}
                        >
                            Sign
                        </Button>
                    </Segment>
                    <Segment stacked>
                        <input
                            type = "text"
                            value = { controller.url }
                            onChange = {( event ) => { controller.setURL ( event.target.value )}}
                        />
                        <div className = "ui hidden divider" ></div>
                        <Button
                            color = "orange"
                            fluid size = "large"
                            onClick = {() => { controller.post ()}}
                        >
                            Post
                        </Button>
                    </Segment>
                </Form>
                </Grid.Column>
            </Grid>
        </div>
    );
});

export default DebugCryptoKey;
