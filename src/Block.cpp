//
//  main.cpp
//  consensus
//
//  Created by Patrick Meehan on 4/27/18.
//  Copyright © 2018 Patrick Meehan. All rights reserved.
//

#include "Block.h"

namespace Volition {

//================================================================//
// Block
//================================================================//

//----------------------------------------------------------------//
Block::Block () {
}

//----------------------------------------------------------------//
Block::~Block () {
}

//================================================================//
// overrides
//================================================================//

//----------------------------------------------------------------//
void Block::AbstractHashable_hash ( Poco::DigestOutputStream& digestStream ) const {

    digestStream << "A";
}

//----------------------------------------------------------------//
void Block::AbstractSerializable_fromJSON ( const Poco::JSON::Object& object ) {
}

//----------------------------------------------------------------//
void Block::AbstractSerializable_toJSON ( Poco::JSON::Object& object ) const {
}


} // namespace Volition
