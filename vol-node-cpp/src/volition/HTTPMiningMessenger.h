// Copyright (c) 2017-2018 Cryptogogue, Inc. All Rights Reserved.
// http://cryptogogue.com

#ifndef VOLITION_HTTPMININGMESSENGER_H
#define VOLITION_HTTPMININGMESSENGER_H

#include <volition/common.h>
#include <volition/AbstractMiningMessenger.h>

namespace Volition {

//================================================================//
// HTTPMiningMessenger
//================================================================//
class HTTPMiningMessenger :
    public virtual AbstractMiningMessenger {
protected:
    
    Poco::ThreadPool        mTaskManagerThreadPool;
    Poco::TaskManager       mTaskManager;

    //----------------------------------------------------------------//
    void        onTaskFinishedNotification                  ( Poco::TaskFinishedNotification* pNf );

    //----------------------------------------------------------------//
    void        AbstractMiningMessenger_requestBlock        ( AbstractMiningMessengerClient& client, string minerID, string url, size_t height ) override;

public:

    //----------------------------------------------------------------//
                HTTPMiningMessenger         ();
                ~HTTPMiningMessenger        ();

};

} // namespace Volition
#endif
