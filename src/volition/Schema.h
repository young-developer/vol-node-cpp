// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_SCHEMA_H
#define VOLITION_SCHEMA_H

#include <volition/common.h>
#include <volition/AssetDefinition.h>
#include <volition/AssetMethod.h>
#include <volition/Ledger.h>
#include <volition/SquapFactory.h>

namespace Volition {

//================================================================//
// Schema
//================================================================//
class Schema :
    public AbstractSerializable {
private:

    friend class Ledger;
    friend class LuaContext;

    typedef SerializableMap < string, AssetDefinition >     Definitions;
    typedef SerializableMap < string, AssetMethod >         Methods;

    string                  mName;
    Definitions             mDefinitions;
    Methods                 mMethods;
    string                  mLua;

    //----------------------------------------------------------------//
    void AbstractSerializable_serializeFrom ( const AbstractSerializerFrom& serializer ) {

        serializer.serialize ( "name",              this->mName );
        serializer.serialize ( "definitions",       this->mDefinitions );
        serializer.serialize ( "methods",           this->mMethods );
        serializer.serialize ( "lua",               this->mLua );
    }

    //----------------------------------------------------------------//
    void AbstractSerializable_serializeTo ( AbstractSerializerTo& serializer ) const {

        serializer.serialize ( "name",              this->mName );
        serializer.serialize ( "definitions",       this->mDefinitions );
        serializer.serialize ( "methods",           this->mMethods );
        serializer.serialize ( "lua",               this->mLua );
    }
};

} // namespace Volition
#endif
