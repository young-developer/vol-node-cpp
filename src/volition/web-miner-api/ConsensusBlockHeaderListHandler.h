// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_WEBMINERAPI_CONSENSUSBLOCKHEADERLISTHANDLER_H
#define VOLITION_WEBMINERAPI_CONSENSUSBLOCKHEADERLISTHANDLER_H

#include <volition/Block.h>
#include <volition/AbstractMinerAPIRequestHandler.h>
#include <volition/Release.h>
#include <volition/TheTransactionBodyFactory.h>

namespace Volition {
namespace WebMinerAPI {

//================================================================//
// ConsensusBlockHeaderListHandler
//================================================================//
class ConsensusBlockHeaderListHandler :
    public AbstractMinerAPIRequestHandler {
public:

    static const size_t HEADER_BATCH_SIZE = 256;

    SUPPORTED_HTTP_METHODS ( HTTP::GET )

    //----------------------------------------------------------------//
    HTTPStatus AbstractMinerAPIRequestHandler_handleRequest ( HTTP::Method method, shared_ptr < Miner > miner, const Poco::JSON::Object& jsonIn, Poco::JSON::Object& jsonOut ) const override {
        UNUSED ( method );
        UNUSED ( jsonIn );
        
        {
            ScopedSharedMinerLedgerLock ledger ( miner );
            ledger.seek ( this->optQuery ( "at", ledger.countBlocks ()));
            
            size_t totalBlocks = ledger.countBlocks ();
            
            size_t max = this->optQuery ( "max", HEADER_BATCH_SIZE );
            u64 release  = this->optQuery ( "accepted-release", 0 );
            
            size_t base = this->optQuery ( "height", 0 );
            base = base < totalBlocks ? base : totalBlocks - 1;
            
            size_t top = base + max;
            top = top <= totalBlocks ? top : totalBlocks;
            
            SerializableList < SerializableSharedConstPtr < BlockHeader >> headers;
            for ( size_t i = base; i < top; ++i ) {
                shared_ptr < const BlockHeader > header = ledger.getHeader ( i );
                if ( release < header->getRelease ()) break;
                if ( !header ) break;
                headers.push_back ( header );
            }
            
            jsonOut.set ( "headers",        ToJSONSerializer::toJSON ( headers ));
        }
        
        {
            ScopedSharedMinerStatusLock minerStatus ( miner );
            jsonOut.set ( "acceptedRelease",    minerStatus.mAcceptedRelease );
            jsonOut.set ( "nextRelease",        VOL_NODE_RELEASE );
        }
        return Poco::Net::HTTPResponse::HTTP_OK;
    }
};

} // namespace TheWebMinerAPI
} // namespace Volition
#endif
