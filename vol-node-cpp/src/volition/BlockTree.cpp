// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#include <volition/Block.h>
#include <volition/BlockTree.h>

namespace Volition {

//================================================================//
// BlockTreeSegment
//================================================================//

//----------------------------------------------------------------//
size_t BlockTreeSegment::getFullLength () const {

    return ( this->mTop->getHeight () - this->mHead->getHeight ());
}

//----------------------------------------------------------------//
size_t BlockTreeSegment::getRewriteDefeatCount () const {

    time_t window = this->mHead->getRewriteWindow (); // TODO: account for different rewrite windows in segment
    return ( size_t )ceil ( difftime ( this->mTop->getTime (), this->mHead->getTime ()) / window );
}

//----------------------------------------------------------------//
size_t BlockTreeSegment::getSegLength () const {

    return this->mTail->getHeight () - this->mHead->getHeight ();
}

//================================================================//
// BlockTreeRoot
//================================================================//

//----------------------------------------------------------------//
size_t BlockTreeFork::getSegLength () const {

    return this->mSeg0.getSegLength ();
}

//================================================================//
// BlockTree
//================================================================//

//----------------------------------------------------------------//
BlockTree::BlockTree () :
    mRoot ( NULL ) {
}

//----------------------------------------------------------------//
BlockTree::~BlockTree () {

    map < string, BlockTreeNode* >::iterator nodeIt = this->mNodes.begin ();
    for ( ; nodeIt != this->mNodes.end (); ++nodeIt ) {
        nodeIt->second->mTree = NULL;
    }
}

//----------------------------------------------------------------//
BlockTreeFork BlockTree::findFork ( const BlockTreeCursor& cursor0, const BlockTreeCursor& cursor1 ) const {

    const BlockTreeNode* node0 = this->findNodeForHash ( cursor0.getHash ());
    const BlockTreeNode* node1 = this->findNodeForHash ( cursor1.getHash ());

    BlockTreeFork root;

    if ( node0->mTree && ( node0->mTree == node1->mTree )) {
    
        BlockTreeSegment seg0;
        BlockTreeSegment seg1;
    
        seg0.mTop = node0;
        seg1.mTop = node1;
    
        size_t height0 = node0->mHeader->getHeight ();
        size_t height1 = node1->mHeader->getHeight ();

        size_t height = height0 < height1 ? height0 : height1;
        
        while ( node0->mParent && ( height < node0->mHeader->getHeight ())) {
            node0 = node0->mParent.get ();
        }
        
        while ( node1->mParent && ( height < node1->mHeader->getHeight ())) {
            node1 = node1->mParent.get ();
        }

        seg0.mHead = node0;
        seg1.mHead = node1;

        seg0.mTail = node0;
        seg1.mTail = node1;

        while ( node0 != node1 ) {
        
            seg0.mHead = node0;
            seg1.mHead = node1;
            
            node0 = node0->mParent.get ();
            node1 = node1->mParent.get ();
        }
        
        assert ( node0 && node1 );
        
        root.mSeg0 = seg0;
        root.mSeg1 = seg1;
        root.mRoot = node0;
        
        assert ( root.mSeg0.getSegLength () == root.mSeg1.getSegLength ());
    }
    return root;
}

//----------------------------------------------------------------//
BlockTreeNode* BlockTree::findNodeForHash ( string hash ) {

    map < string, BlockTreeNode* >::iterator nodeIt = this->mNodes.find ( hash );
    if ( nodeIt != this->mNodes.end ()) return nodeIt->second;
    return NULL;
}

//----------------------------------------------------------------//
const BlockTreeNode* BlockTree::findNodeForHash ( string hash ) const {

    map < string, BlockTreeNode* >::const_iterator nodeIt = this->mNodes.find ( hash );
    if ( nodeIt != this->mNodes.cend ()) return nodeIt->second;
    return NULL;
}

//----------------------------------------------------------------//
void BlockTree::logTree ( string prefix, size_t maxDepth ) const {

    this->logTreeRecurse ( prefix, maxDepth, this->mRoot, 0 );
}

//----------------------------------------------------------------//
void BlockTree::logTreeRecurse ( string prefix, size_t maxDepth, const BlockTreeNode* node, size_t depth ) const {

    if ( !node ) return;
    if (( maxDepth > 0 ) && ( depth >= maxDepth )) return;

    string str;
    Format::write_indent ( str, ".   ", depth );
    
    int i = 0;
    do {
        const BlockHeader& header = *node->mHeader;
        Format::write ( str, "%s[%s]", ( i > 0 ) ? "," : "", ( header.getHeight () > 0 ) ? header.getMinerID ().c_str () : "-" );
        node = ( node->mChildren.size () > 0 ) ? *node->mChildren.begin () : NULL;
        ++i;
    }
    while ( node && ( node->mChildren.size () <= 1 ));
    LGN_LOG ( VOL_FILTER_ROOT, INFO, "%s%s", prefix.c_str (), str.c_str ());
    
    if ( node ) {
        ++depth;
        set < BlockTreeNode* >::const_iterator childIt = node->mChildren.begin ();
        for ( ; childIt != node->mChildren.end (); ++ childIt ) {
            this->logTreeRecurse ( prefix, maxDepth, *childIt, depth );
        }
    }
}

//================================================================//
// virtual
//================================================================//

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_affirm ( BlockTreeTag& tag, shared_ptr < const BlockHeader > header, shared_ptr < const Block > block, bool isProvisional ) {

    string tagName = tag.mTagName;
    assert ( tagName.size ());
    assert (( tag.mBlockTree == NULL ) || ( tag.mBlockTree == this ));
    
    tag.mBlockTree = this;

    if ( !header ) return BlockTreeCursor ();

    string hash = header->getDigest ().toHex ();
    if ( block ) {
        assert ( hash == block->getDigest ().toHex ());
    }
    
    BlockTreeNode::Ptr node = this->findNodeForHash ( hash );

    if ( node ) {
        this->mTags [ tagName ] = node->shared_from_this ();
    }
    else {

        string prevHash = header->getPrevDigest ();
        BlockTreeNode* prevNode = this->findNodeForHash ( prevHash );

        if ( !prevNode && this->mRoot ) return BlockTreeCursor ();

        shared_ptr < BlockTreeNode > shared = make_shared < BlockTreeNode >();
        node = shared.get ();

        node->mTree         = this;
        node->mHeader       = header;
        node->mStatus       = BlockTreeNode::STATUS_NEW;
        node->mMeta         = isProvisional ? BlockTreeNode::META_PROVISIONAL : BlockTreeNode::META_NONE;

        if ( prevNode ) {
        
            node->mParent = prevNode->shared_from_this ();
            prevNode->mChildren.insert ( node );
            
            if (( node->mParent->mStatus == BlockTreeNode::STATUS_MISSING ) || ( node->mParent->mStatus == BlockTreeNode::STATUS_INVALID )) {
                node->mStatus = node->mParent->mStatus;
            }
        }
        else {
            this->mRoot = node;
        }
        this->mNodes [ hash ] = node;
        this->mTags [ tagName ] = shared;
    }
    
    this->update ( block );
    return *node;
}

//----------------------------------------------------------------//
AbstractBlockTree::CanAppend BlockTree::AbstractBlockTree_checkAppend ( const BlockHeader& header ) const {

    if ( header.getHeight () < this->mRoot->getHeight ()) return REFUSED;

    string hash = header.getDigest ().toHex ();

    BlockTreeNode::ConstPtr node = this->findNodeForHash ( hash );
    if ( node ) return ALREADY_EXISTS;

    if ( !node ) {

        string prevHash = header.getPrevDigest ();
        BlockTreeNode::ConstPtr prevNode = this->findNodeForHash ( prevHash );

        if ( !prevNode ) return MISSING_PARENT;
        if ( prevNode->mMeta == BlockTreeNode::META_REFUSED ) return REFUSED;
        if ( header.getTime () < prevNode->getNextTime ()) return TOO_SOON;
    }
    return APPEND_OK;
}

//----------------------------------------------------------------//
int BlockTree::AbstractBlockTree_compare ( const BlockTreeCursor& cursor0, const BlockTreeCursor& cursor1, BlockTreeCursor::RewriteMode rewriteMode ) const {

    BlockTreeFork root = this->findFork ( cursor0, cursor1);

    size_t fullLength0  = root.mSeg0.getFullLength ();
    size_t fullLength1  = root.mSeg1.getFullLength ();

    if ( rewriteMode == BlockTreeCursor::REWRITE_WINDOW ) {
        
        size_t segLength = root.getSegLength (); // length of the shorter segment (if different lengths)
        
        // if one chain is shorter, it must have enough blocks to "defeat" the longer chain (as a function of time)
        if (( segLength < fullLength0 ) && ( segLength < root.mSeg0.getRewriteDefeatCount ())) return -1;
        if (( segLength < fullLength1 ) && ( segLength < root.mSeg1.getRewriteDefeatCount ())) return 1;
    }

    int score = 0;

    const BlockTreeNode* node0 = root.mSeg0.mTail;
    const BlockTreeNode* node1 = root.mSeg1.mTail;

    while ( node0 != node1 ) {
    
        score += BlockHeader::compare ( *node0->mHeader, *node1->mHeader );
        
        node0 = node0->mParent.get ();
        node1 = node1->mParent.get ();
    }

    if (( score == 0 ) && ( fullLength0 != fullLength1 )) {
        return ( fullLength0 < fullLength1 ) ? 1 : -1;
    }
    return score < 0 ? -1 : score > 0 ? 1 : 0;
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_findCursorForHash ( string hash ) const {

    map < string, BlockTreeNode* >::const_iterator nodeIt = this->mNodes.find ( hash );
    if ( nodeIt != this->mNodes.cend ()) return *nodeIt->second;
    return BlockTreeCursor ();
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_findCursorForTag ( const BlockTreeTag& tag ) const {

    assert ( tag.mTagName.size () > 0 );

    if ( tag.mBlockTree ) {

        assert ( tag.mBlockTree == this );

        map < string, shared_ptr < BlockTreeNode >>::const_iterator nodeIt = this->mTags.find ( tag.mTagName );
        if ( nodeIt != this->mTags.cend ()) return *nodeIt->second;
    }
    return BlockTreeCursor ();
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_findRoot ( const BlockTreeCursor& cursor0, const BlockTreeCursor& cursor1 ) const {

    BlockTreeFork fork = this->findFork ( cursor0, cursor1 );
    return *fork.mRoot;
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_getParent ( const BlockTreeCursor& cursor ) const {

    string hash = cursor.getHash ();
    const BlockTreeNode* node = this->findNodeForHash ( hash );

    if ( node && node->mParent ) {
        return *node->mParent;
    }
    return BlockTreeCursor ();
}

//----------------------------------------------------------------//
void BlockTree::AbstractBlockTree_mark ( const BlockTreeCursor& cursor, BlockTreeCursor::Status status ) {

    BlockTreeNode* node = this->findNodeForHash ( cursor.getHash ());

    if ( node ) {
        node->mark ( status );
    }
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_tag ( BlockTreeTag& tag, const BlockTreeCursor& cursor ) {

    string tagName = tag.mTagName;
    assert ( tagName.size ());
    
    assert (( tag.mBlockTree == NULL ) || ( tag.mBlockTree == this ));
    
    if ( cursor.mTree ) {
    
        assert ( cursor.mTree == this );
        tag.mBlockTree = this;
        
        BlockTreeNode::Ptr node = this->findNodeForHash ( cursor.getHash ());
        assert ( node );
        this->mTags [ tagName ] = node->shared_from_this ();
        
        return *node;
    }
    return BlockTreeCursor ();
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_tag ( BlockTreeTag& tag, const BlockTreeTag& otherTag ) {

    assert ( tag.mTagName.size ());
    assert ( otherTag.mTagName.size ());

    assert (( tag.mBlockTree == NULL ) || ( tag.mBlockTree == this ));
        
    if ( otherTag.mBlockTree ) {
    
        assert ( otherTag.mBlockTree == this );
        tag.mBlockTree = this;
        
        map < string, shared_ptr < BlockTreeNode >>::const_iterator nodeIt = this->mTags.find ( otherTag.mTagName );
        assert ( nodeIt != this->mTags.cend ());
        this->mTags [ tag.mTagName ] = nodeIt->second;
        
        return *nodeIt->second;
    }
    return BlockTreeCursor ();
}

//----------------------------------------------------------------//
BlockTreeCursor BlockTree::AbstractBlockTree_update ( shared_ptr < const Block > block ) {

    if ( !block ) return BlockTreeCursor ();
    string hash = block->getDigest ();

    BlockTreeNode::Ptr node = this->findNodeForHash ( hash );
    if ( !node ) return BlockTreeCursor ();
    
    assert ( node->mHeader );
    assert ( node->mHeader->getDigest ().toHex () == hash );
    
    node->mBlock = block;
    node->markComplete ();
    
    return *node;
}

} // namespace Volition
