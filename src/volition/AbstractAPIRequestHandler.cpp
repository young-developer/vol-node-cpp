// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/AbstractAPIRequestHandler.h>
#include <volition/FNV1a.h>

namespace Volition {

typedef Poco::Net::HTTPResponse::HTTPStatus HTTPStatus;

//================================================================//
// AbstractAPIRequestHandler
//================================================================//

//----------------------------------------------------------------//
AbstractAPIRequestHandler::AbstractAPIRequestHandler () {
}

//----------------------------------------------------------------//
AbstractAPIRequestHandler::~AbstractAPIRequestHandler () {
}

//================================================================//
// overrides
//================================================================//

//----------------------------------------------------------------//
HTTP::Method AbstractAPIRequestHandler::AbstractAPIRequestHandler_getSupportedHTTPMethods () const {
    return HTTP::ALL;
}

//----------------------------------------------------------------//
HTTPStatus AbstractAPIRequestHandler::AbstractAPIRequestHandler_handleDelete () const {
    return Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED;
}

//----------------------------------------------------------------//
HTTPStatus AbstractAPIRequestHandler::AbstractAPIRequestHandler_handleGet ( Poco::JSON::Object& jsonOut ) const {
    UNUSED ( jsonOut );
    return Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED;
}

//----------------------------------------------------------------//
HTTPStatus AbstractAPIRequestHandler::AbstractAPIRequestHandler_handlePatch ( const Poco::JSON::Object& jsonIn, Poco::JSON::Object& jsonOut ) const {
    UNUSED ( jsonIn );
    UNUSED ( jsonOut );
    return Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED;
}

//----------------------------------------------------------------//
HTTPStatus AbstractAPIRequestHandler::AbstractAPIRequestHandler_handlePost ( const Poco::JSON::Object& jsonIn, Poco::JSON::Object& jsonOut ) const {
    UNUSED ( jsonIn );
    UNUSED ( jsonOut );
    return Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED;
}

//----------------------------------------------------------------//
HTTPStatus AbstractAPIRequestHandler::AbstractAPIRequestHandler_handlePut ( const Poco::JSON::Object& jsonIn, Poco::JSON::Object& jsonOut ) const {
    UNUSED ( jsonIn );
    UNUSED ( jsonOut );
    return Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED;
}

//----------------------------------------------------------------//
HTTPStatus AbstractAPIRequestHandler::AbstractAPIRequestHandler_handleRequest ( HTTP::Method method, const Poco::JSON::Object& jsonIn, Poco::JSON::Object& jsonOut ) const {

    switch ( method ) {
        case HTTP::DELETE:  return this->AbstractAPIRequestHandler_handleDelete ();
        case HTTP::GET:     return this->AbstractAPIRequestHandler_handleGet ( jsonOut );
        case HTTP::PATCH:   return this->AbstractAPIRequestHandler_handlePatch ( jsonIn, jsonOut );
        case HTTP::POST:    return this->AbstractAPIRequestHandler_handlePost ( jsonIn, jsonOut );
        case HTTP::PUT:     return this->AbstractAPIRequestHandler_handlePut ( jsonIn, jsonOut );
        default:
            break;
    }
    return Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED;
}

//----------------------------------------------------------------//
void AbstractAPIRequestHandler::AbstractRequestHandler_handleRequest ( const Routing::PathMatch& match, Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response ) const {
    UNUSED ( match );

    LGN_LOG ( VOL_FILTER_HTTP, INFO, "%p: %s REQUEST %s", ( void* )this, request.getMethod ().c_str (), request.getURI ().c_str ());

    try {

        chrono::high_resolution_clock::time_point t0 = chrono::high_resolution_clock::now ();

        response.add ( "Access-Control-Allow-Origin", "*" );
        response.add ( "Access-Control-Allow-Headers", "Accept, Content-Type, Origin, X-Requested-With" );
        response.add ( "Access-Control-Allow-Methods", "DELETE, GET, HEAD, OPTIONS, POST, PUT" );

        HTTP::Method method = HTTP::getMethodForString ( request.getMethod ());
        
        if ( method == HTTP::OPTIONS ) {
            response.setStatus ( Poco::Net::HTTPResponse::HTTP_OK );
            response.send ();
            return;
        }
        
        if ( !( this->AbstractAPIRequestHandler_getSupportedHTTPMethods () & method )) {
            response.setStatus ( Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED );
            response.send ();
            return;
        }

        Poco::JSON::Object::Ptr jsonIn  = NULL;
        Poco::JSON::Object::Ptr jsonOut = new Poco::JSON::Object ();
        
        if ( method & ( HTTP::POST | HTTP::PUT | HTTP::PATCH )) {
        
            Poco::JSON::Parser parser;
            Poco::Dynamic::Var result = parser.parse ( request.stream ());
            jsonIn = result.extract < Poco::JSON::Object::Ptr >();
        
            if ( !jsonIn ) {
                response.setStatus ( Poco::Net::HTTPResponse::HTTP_BAD_REQUEST );
                response.send ();
                return;
            }
        }
        else {
            jsonIn = new Poco::JSON::Object ();
        }

        HTTPStatus status = this->AbstractAPIRequestHandler_handleRequest ( method, *jsonIn, *jsonOut );
        response.setStatus ( status );
        response.setContentType ( "application/json" );
        
        ostream& out = response.send ();
        jsonOut->stringify ( out, 4, -1 );
        out.flush ();
        
        chrono::high_resolution_clock::time_point t1 = chrono::high_resolution_clock::now ();
        chrono::milliseconds span = chrono::duration_cast < chrono::milliseconds >( t1 - t0 );

        // TODO: the cast to int here is slightly gross, but is a quick fix for a build warning on some platforms (where u64 is defined as a long instead of a long long).
        LGN_LOG ( VOL_FILTER_HTTP, INFO, "%p: %dms %s RESPONSE %s", ( void* )this, ( int )span.count (), request.getMethod ().c_str (), request.getURI ().c_str ());
    }
    catch ( const Poco::Exception& exc ) {
        LGN_LOG ( VOL_FILTER_HTTP, INFO, "%p: EXCEPTION in %s %s", ( void* )this, request.getMethod ().c_str (), request.getURI ().c_str ());
        LGN_LOG ( VOL_FILTER_CONSENSUS, INFO, "%s", exc.displayText ().c_str ());
    }
    catch ( ... ) {
        LGN_LOG ( VOL_FILTER_HTTP, INFO, "%p: EXCEPTION in %s %s", ( void* )this, request.getMethod ().c_str (), request.getURI ().c_str ());
        response.setStatus ( Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR );
    }
}
    
} // namespace Volition
