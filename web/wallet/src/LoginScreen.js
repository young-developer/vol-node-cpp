/* eslint-disable no-whitespace-before-property */

import { withAppStateAndUser }  from './AppStateProvider';
import BaseComponent            from './BaseComponent';
import * as bcrypt              from 'bcryptjs';
import React                    from 'react';
import { Button, Form, Grid, Header, Segment } from 'semantic-ui-react';

//================================================================//
// LoginScreen
//================================================================//
class LoginScreen extends BaseComponent {

    //----------------------------------------------------------------//
    constructor ( props ) {
        super ( props );

        this.state = {
            password: '',
            errorMessage: '',
        };
    }

    //----------------------------------------------------------------//
    handleChange ( event ) {

        this.setState ({[ event.target.name ]: event.target.value });
    }

    //----------------------------------------------------------------//
    handleSubmit () {

        const { appState } = this.props;
        const passwordHash = ( appState.state.user && appState.state.user.passwordHash ) || '';

        console.log ( 'Logging in' );

        if ( bcrypt.compareSync ( this.state.password, passwordHash )) {

            console.log ( 'Login success!' );
            console.log ( 'Login State:', this.state );

            this.props.appState.login ( true );
        }
        else {
            this.setState ({ errorMessage : 'Invalid password.' });
        }
    }

    //----------------------------------------------------------------//
    render () {

        const { appState } = this.props;
        const { errorMessage, password } = this.state;
        const isEnabled = password.length > 0;

        // TODO: move redirects to a HOC
        if ( !appState.hasUser ()) return this.redirect ( '/' );
        if ( appState.isLoggedIn ()) return this.redirect ( '/accounts' );

        let onChange        = ( event ) => { this.handleChange ( event )};
        let onSubmit        = () => { this.handleSubmit ()};

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
                        Login to your wallet.
                    </Header>
                    <Form size = "large" onSubmit = { onSubmit }>
                        <Segment stacked>
                            <Form.Input
                                fluid
                                icon = "lock"
                                iconPosition = "left"
                                placeholder = "Password"
                                type = "password"
                                name = "password"
                                value = { this.state.password }
                                onChange = { onChange }
                                error = {( errorMessage.length > 0 ) ? true : false}
                            />
                            {( errorMessage.length > 0 ) && <span>{ errorMessage }</span>}
                            <Button color = "teal" fluid size = "large" disabled = { !isEnabled }>
                                Login
                            </Button>
                        </Segment>
                    </Form>
                    </Grid.Column>
                </Grid>
            </div>
        );
    }
}
export default withAppStateAndUser ( LoginScreen );