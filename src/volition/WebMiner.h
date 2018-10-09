// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_WEBMINER_H
#define VOLITION_WEBMINER_H

#include <volition/common.h>
#include <volition/AbstractTransaction.h>
#include <volition/Block.h>
#include <volition/Chain.h>
#include <volition/Miner.h>
#include <volition/State.h>

namespace Volition {

class SyncChainTask;

//================================================================//
// RemoteMiner
//================================================================//
class RemoteMiner {
private:

    friend class WebMiner;

    string                  mURL;
    size_t                  mCurrentBlock;

    bool                    mWaitingForTask;

public:

    //----------------------------------------------------------------//
                    RemoteMiner             ();
                    ~RemoteMiner            ();
};

//================================================================//
// BlockQueueEntry
//================================================================//
class BlockQueueEntry {
private:

    friend class WebMiner;
    friend class SyncChainTask;

    string      mMinerID;
    Block       mBlock;
    bool        mHasBlock;
};

//================================================================//
// WebMiner
//================================================================//
class WebMiner :
    public Miner,
    public Poco::Activity < WebMiner > {
private:

    Poco::Mutex                         mMutex;

    Poco::TaskManager                   mTaskManager;
    set < string >                      mMinerSet;
    map < string, RemoteMiner >         mRemoteMiners;

    bool                                        mSolo;
    list < unique_ptr < BlockQueueEntry >>      mBlockQueue;

    //----------------------------------------------------------------//
    void            onSyncChainNotification     ( Poco::TaskFinishedNotification* pNf );
    void            processQueue                ();
    void            run                         () override;
    void            runMulti                    ();
    void            runSolo                     ();
    void            startTasks                  ();
    void            updateMiners                ();

public:

    //----------------------------------------------------------------//
    Poco::Mutex&    getMutex                ();
    void            setSolo                 ( bool solo );
    void            shutdown                ();
                    WebMiner                ();
                    ~WebMiner               ();
};

} // namespace Volition
#endif
