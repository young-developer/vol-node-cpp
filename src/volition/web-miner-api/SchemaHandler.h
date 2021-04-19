// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_WEBMINERAPI_SCHEMAHANDLER_H
#define VOLITION_WEBMINERAPI_SCHEMAHANDLER_H

#include <volition/Block.h>
#include <volition/Schema.h>
#include <volition/AbstractMinerAPIRequestHandler.h>
#include <volition/TheTransactionBodyFactory.h>

namespace Volition {
namespace WebMinerAPI {

//================================================================//
// SchemaHandler
//================================================================//
class SchemaHandler :
    public AbstractMinerAPIRequestHandler {
public:

    SUPPORTED_HTTP_METHODS ( HTTP::GET )

    //----------------------------------------------------------------//
    HTTPStatus AbstractMinerAPIRequestHandler_handleRequest ( HTTP::Method method, shared_ptr < Miner > miner, const Poco::JSON::Object& jsonIn, Poco::JSON::Object& jsonOut ) const override {
        UNUSED ( method );
        UNUSED ( jsonIn );
        
        ScopedSharedMinerLedgerLock ledger ( miner );
        ledger.seek ( this->optQuery ( "at", ledger.countBlocks ()));
        
        const Schema& schema = ledger.getSchema ();

        jsonOut.set ( "schema",         ToJSONSerializer::toJSON ( schema ));
        jsonOut.set ( "schemaHash",     ledger.getSchemaHash ());

        return Poco::Net::HTTPResponse::HTTP_OK;
    }
};

} // namespace TheWebMinerAPI
} // namespace Volition
#endif
