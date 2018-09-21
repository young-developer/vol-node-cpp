// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/State.h>
#include <volition/VersionedStore.h>
#include <volition/VersionedStoreEpoch.h>

//#define DEBUG_LOG printf ( "%04x:  ", ( int )(( size_t )this ) & 0xffff ); printf
#define DEBUG_LOG(...)

namespace Volition {

//================================================================//
// VersionedStoreEpoch
//================================================================//

//----------------------------------------------------------------//
void VersionedStoreEpoch::affirmChild ( VersionedStoreEpoch& child ) {

    this->mChildren.insert ( &child );
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::affirmClient ( VersionedStore& client ) {

    this->mClients.insert ( &client );
}

//----------------------------------------------------------------//
size_t VersionedStoreEpoch::countDependencies () const {

    return ( this->mClients.size () + this->mChildren.size ());
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::eraseChild ( VersionedStoreEpoch& child ) {

    this->mChildren.erase ( &child );
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::eraseClient ( VersionedStore& client ) {

    this->mClients.erase ( &client );
}

//----------------------------------------------------------------//
size_t VersionedStoreEpoch::findImmutableTop ( const VersionedStore* ignore ) const {

    DEBUG_LOG ( "VersionedStoreEpoch::findImmutableTop ()\n" );

    size_t immutableTop = this->getVersionDependency ();

    set < VersionedStore* >::const_iterator clientIt = this->mClients.cbegin ();
    for ( ; clientIt != this->mClients.cend (); ++clientIt ) {

        const VersionedStore* client = *clientIt;
        if ( client != ignore ) {
        
            size_t clientVersion = client->getVersionDependency ();
            
            if ( clientVersion > immutableTop ) {
                immutableTop = clientVersion;
            }
        }
    }
    
    set < VersionedStoreEpoch* >::const_iterator childIt = this->mChildren.cbegin ();
    for ( ; childIt != this->mChildren.cend (); ++childIt ) {

        const VersionedStoreEpoch* child = *childIt;
        
        size_t clientVersion = child->getVersionDependency ();
            
        if ( clientVersion > immutableTop ) {
            immutableTop = clientVersion;
        }
    }
    
    DEBUG_LOG ( "  immutableTop: %d\n", ( int )immutableTop );
    
    return immutableTop;
}

//----------------------------------------------------------------//
const AbstractValueStack* VersionedStoreEpoch::findValueStack ( string key ) const {

    map < string, unique_ptr < AbstractValueStack >>::const_iterator valueIt = this->mValueStacksByKey.find ( key );
    return ( valueIt != this->mValueStacksByKey.cend ()) ? valueIt->second.get () : NULL;
}

//----------------------------------------------------------------//
const void* VersionedStoreEpoch::getRaw ( size_t version, string key, size_t typeID ) const {

    const VersionedStoreEpoch* epoch = this;
    for ( ; epoch; version = epoch->mBaseVersion, epoch = epoch->mParent.get ()) {
        if ( epoch->mBaseVersion <= version ) {
        
            const AbstractValueStack* valueStack = epoch->findValueStack ( key );
            
            if ( valueStack ) {
                assert ( valueStack->mTypeID == typeID );
                const void* value = valueStack->getRaw ( version );
                if ( value ) {
                    return value;
                }
            }
        }
    }
    return NULL;
}

//----------------------------------------------------------------//
size_t VersionedStoreEpoch::getTopVersion () const {

    return this->mEpochLayers.size () ? this->mEpochLayers.rbegin ()->first + 1 : 0;
}

//----------------------------------------------------------------//
size_t VersionedStoreEpoch::getVersionDependency () const {

    return this->mBaseVersion;
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::optimize () {

    DEBUG_LOG ( "VersionedStoreEpoch::optimize ()\n" );

    VersionedStore* topClient = NULL;
    VersionedStoreEpoch* topChild = NULL;

    for ( set < VersionedStore* >::iterator clientIt = this->mClients.begin (); clientIt != this->mClients.end (); ++clientIt ) {

        VersionedStore* client = *clientIt;
        if (( topClient == NULL ) || ( topClient->getVersionDependency () < client->getVersionDependency ())) {
            topClient = client;
            DEBUG_LOG ( "  topClient: %04x version: %d\n", ( int )(( size_t )client ) & 0xffff, ( int ) client->getVersionDependency ());
        }
    }
    
    for ( set < VersionedStoreEpoch* >::iterator childIt = this->mChildren.begin (); childIt != this->mChildren.end (); ++childIt ) {
        
        VersionedStoreEpoch* child = *childIt;
        
        bool replace = ( topChild == NULL ) || ( topChild->getVersionDependency () < child->getVersionDependency ());
        replace = replace || (( topChild->getVersionDependency () == child->getVersionDependency ()) && ( topChild->getTopVersion () < child->getTopVersion ()));
        
        if ( replace ) {
            topChild = child;
            DEBUG_LOG ( "  topClient: %04x topChild: %d\n", ( int )(( size_t )child ) & 0xffff, ( int ) child->getVersionDependency ());
        }
    }

    size_t immutableTop = topClient ? ( topClient->getVersionDependency ()) : 0;
    if ( topChild && ( immutableTop < topChild->getVersionDependency ())) {
        immutableTop = topChild->getVersionDependency ();
    }
    
    DEBUG_LOG ( "  immutableTop: %d\n", ( int )immutableTop );
    DEBUG_LOG ( "  topVersion: %d\n", ( int )this->getTopVersion ());
    
    if ( immutableTop < this->getTopVersion ()) {
        
        size_t topVersion = this->getTopVersion ();
        for ( size_t i = immutableTop; i < topVersion; ++i ) {
            this->popLayer ();
        }
        assert ( this->getTopVersion () == immutableTop );
    }
    
    if ( topChild && (( topClient == NULL ) || (( topClient->getVersionDependency ()) <= topChild->getVersionDependency ()))) {
    
        DEBUG_LOG ( "  MERGING CHILD EPOCH\n" );
    
        shared_ptr < VersionedStoreEpoch > mergeEpoch = topChild->shared_from_this ();
        weak_ptr < VersionedStoreEpoch > weakMergeEpoch = mergeEpoch;
    
        // merge the epoch layers
        this->mEpochLayers.insert ( mergeEpoch->mEpochLayers.begin(), mergeEpoch->mEpochLayers.end ());
        
        // copy the value stacks
        map < string, unique_ptr < AbstractValueStack >>::iterator valueStackIt = mergeEpoch->mValueStacksByKey.begin ();
        for ( ; valueStackIt != topChild->mValueStacksByKey.end (); ++valueStackIt ) {
            
            const AbstractValueStack* fromStack = mergeEpoch->findValueStack ( valueStackIt->first );
            assert ( fromStack );
            
            unique_ptr < AbstractValueStack >& toStack = this->mValueStacksByKey [ valueStackIt->first ];
            if ( !toStack ) {
                toStack = fromStack->makeEmptyCopy ();
            }
            toStack->copyFrom ( *fromStack );
        }
        
        // copy the clients
        for ( set < VersionedStore* >::iterator clientIt = mergeEpoch->mClients.begin (); clientIt != mergeEpoch->mClients.end (); ++clientIt ) {
            VersionedStore* client = *clientIt;
            this->affirmClient ( *client );
            client->mEpoch = this->shared_from_this ();
        }
        
        // copy the children
        for ( set < VersionedStoreEpoch* >::iterator childIt = mergeEpoch->mChildren.begin (); childIt != mergeEpoch->mChildren.end (); ++childIt ) {
            VersionedStoreEpoch* child = *childIt;
            this->affirmChild ( *child );
            child->mParent = this->shared_from_this ();
        }

        mergeEpoch = NULL;
        assert ( weakMergeEpoch.expired ());
        
        this->optimize ();
    }
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::popLayer () {

    DEBUG_LOG ( "VersionedStoreEpoch::popLayer ()\n" );

    map < size_t, EpochLayer >::reverse_iterator layerIt = this->mEpochLayers.rbegin ();
    if ( layerIt != this->mEpochLayers.rend ()) {
    
        DEBUG_LOG ( "  popping layer: %d\n", ( int )layerIt->first );
        
        EpochLayer& layer = layerIt->second;
        
        EpochLayer::iterator keyIt = layer.begin ();
        for ( ; keyIt != layer.end (); ++keyIt ) {

            unique_ptr < AbstractValueStack >& valueStack = this->mValueStacksByKey [ *keyIt ];
            assert ( valueStack );
            
            valueStack->erase ( layerIt->first );
            if ( valueStack->size () == 0 ) {
                this->mValueStacksByKey.erase ( *keyIt );
            }
        }
        
        this->mEpochLayers.erase ( layerIt->first );
    }
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::setParent ( shared_ptr < VersionedStoreEpoch > parent ) {

    if ( this->mParent != parent ) {

        if ( this->mParent ) {
            this->mParent->eraseChild ( *this );
        }

        this->mParent = parent;

        if ( parent ) {
            parent->affirmChild ( *this );
        }
    }
}

//----------------------------------------------------------------//
void VersionedStoreEpoch::setRaw ( size_t version, string key, const void* value ) {

    this->mValueStacksByKey [ key ]->setRaw ( version, value );
    
    EpochLayer& layer = this->mEpochLayers [ version ];
    if ( layer.find ( key ) == layer.end ()) {
        layer.insert ( key );
    }
}

//----------------------------------------------------------------//
VersionedStoreEpoch::VersionedStoreEpoch () :
    mBaseVersion ( 0 ) {
}

//----------------------------------------------------------------//
VersionedStoreEpoch::VersionedStoreEpoch ( shared_ptr < VersionedStoreEpoch > parent, size_t baseVersion ) {
    
    assert ( parent && ( parent->mBaseVersion <= baseVersion ) && ( baseVersion <= parent->getTopVersion ()));

    this->setParent ( parent->mBaseVersion < baseVersion ? parent : parent->mParent );
    this->mBaseVersion = baseVersion;
    
    map < size_t, EpochLayer >::const_iterator layerIt = parent->mEpochLayers.find ( baseVersion );
    if ( layerIt != parent->mEpochLayers.cend ()) {
    
        const EpochLayer& fromLayer = layerIt->second;
        
        EpochLayer::const_iterator keyIt = fromLayer.cbegin ();
        for ( ; keyIt != fromLayer.cend (); ++keyIt ) {
            
            const AbstractValueStack* fromStack = parent->findValueStack ( *keyIt );
            assert ( fromStack );
            
            unique_ptr < AbstractValueStack >& toStack = this->mValueStacksByKey [ *keyIt ];
            if ( !toStack ) {
                toStack = fromStack->makeEmptyCopy ();
            }
            toStack->setRaw ( baseVersion, fromStack->getRaw ( baseVersion ));
        }
    }
}

//----------------------------------------------------------------//
VersionedStoreEpoch::~VersionedStoreEpoch () {

    this->setParent ( NULL );
}

} // namespace Volition
