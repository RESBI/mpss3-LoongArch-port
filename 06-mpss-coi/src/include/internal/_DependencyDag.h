/*
 * Copyright 2010-2017 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 2.1.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Disclaimer: The codes contained in these modules may be specific
 * to the Intel Software Development Platform codenamed Knights Ferry,
 * and the Intel product codenamed Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel offers no warranty of any kind regarding the code. This code is
 * licensed on an "AS IS" basis and Intel is not obligated to provide
 * any support, assistance, installation, training, or other services
 * of any kind. Intel is also not obligated to provide any updates,
 * enhancements or extensions. Intel specifically disclaims any warranty
 * of merchantability, non-infringement, fitness for any particular
 * purpose, and any other warranty.
 *
 * Further, Intel disclaims all liability of any kind, including but
 * not limited to liability for infringement of any proprietary rights,
 * relating to the use of the code, even if Intel is notified of the
 * possibility of such liability. Except as expressly stated in an Intel
 * license agreement provided with this code and agreed upon with Intel,
 * no license, express or implied, by estoppel or otherwise, to any
 * intellectual property rights is granted herein.
 */

#ifndef _DEPENDENCYDAG_H
#define _DEPENDENCYDAG_H

#include <stdio.h>
#include <list>
#include <map>
    #include <stdint.h>
    #include <pthread.h>
#include "../source/COIEvent_source.h"
#include "../common/COITypes_common.h"
#include "../common/COIResult_common.h"
#include "../internal/_PthreadMutexAutoLock.h"


#define TESTIMPORT

//Enable this macro when Need to print the DAG node Types.
//Used for debugging purpose.
#define PRINT_DAG_NODE_TYPE 0

class TaskNode;

// this is a link list entry node
// it is used by a dag node to construct the list of dependent nodes
struct node_t
{
    node_t() : self(NULL), next(NULL)
    {
        event.opaque[0] = event.opaque[1] = (uint64_t) - 1;
    }
    COIEVENT    event;
    TaskNode   *self;// pointer to the node that owns this link
    node_t     *next;// next node in the list
};

//Contains information about each inuse/refcount/free node "group"
class ProcessStateInfo;

#ifdef DEBUG
    #define MAGIC   0x719ab10d
#endif

// a DAG node. Used by various components to add work to the DAG scheduler
// in order to schedule work to do later once all of its dependencies are met.
class TaskNode
{
private:
    // every new dag gets a unique COIEVENT
    static COIEVENT next_event;
    TaskNode() : userDataSet(false), userData(NULL), callBack(NULL) { }

    //Make copy constructor and assignment operator private
    TaskNode(const TaskNode &node)
    {
        event = node.event;
        userDataSet = node.userDataSet;
        userData = node.userData;
        callBack = node.callBack;
    }
    //Assignment operator
    TaskNode &operator = (const TaskNode &node)
    {
        event = node.event;
        userDataSet = node.userDataSet;
        userData = node.userData;
        callBack = node.callBack;
        return *this;
    }
public:
    volatile bool initiate_active;
#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType() = 0 ;
#endif

    typedef enum
    {
        INVALID_EVENT = (uint64_t) - 1,
        DAG_EVENT = 0,
        USER_EVENT = 1,
        DMA_EVENT = 2
    } EVENT_TYPE;
    static COIEVENT invalid_event;

    // Every task node needs to know how many dependencies (predecessor task
    // nodes) it has at construction time so that we can preallocate linked
    // list node for their dependent list.
    TESTIMPORT TaskNode(int num_deps, EVENT_TYPE type = DAG_EVENT);

    TESTIMPORT virtual ~TaskNode();

    // A reference to the event associated with this task node.
    COIEVENT &GetEvent()
    {
        return event;
    }

    bool initiate_wrapper()
    {
        initiate_active = true;
        bool result = initiate();
        initiate_active = false;
        return result;
    }

    bool GetUserData(const void *&ud)
    {
        ud = userData;
        return userDataSet;
    }
    //Sets User Data for Notification Callback
    static void SetUserData(const void *in_UserData);
    //Clears User Data Once all registered notification callback functions have been unregistered
    static void ClearUserData();
    // Returns the number of tasks that immediately precede this task
    // (depend on this task).
    int Dependencies()
    {
        return number_of_dependencies;
    }

    // once that work is totally done, complete needs to be called to un block
    // its dependents
    void base_complete_impl(bool callback = true);
    TESTIMPORT virtual void complete();

    // The event that this node represents.  the map of all events
    // will map the event to this node.  client code uses this for
    // creating and maintaining dependencies
    COIEVENT  event;
    bool  userDataSet;
    const void  *userData;
    COI_EVENT_CALLBACK callBack;

protected:
    // Perform calling the user callback
    void do_callback();

private:
    // all the TaskNode functions must be called via the DAG functions below
    // adds the node to the DAG graph, can be undone using remove_from_graph
    TESTIMPORT void add_to_graph(int num_deps, const COIEVENT *deps);
    // undoes the add
    void remove_from_graph(int num_deps, const COIEVENT *deps);

    // the node is ready and we can start the work of the node
    virtual bool initiate() = 0;

    // the preallocated linked list entry nodes used by the nodes that this
    // one depends on
    node_t     *donated_nodes;
    // and here is the linked list of node that depend on this one
    node_t     *dependents_list;

    // original number of dependencies passed in
    int         number_of_dependencies;
    // actual number of nodes currently dependent on, decremented every time
    // a node this one depends on is finished.
    // once this number hits zero this node is ready to run and its initiate
    // function will be called
    int         active_dependencies_left;


#ifdef DEBUG
    uint64_t         magic;
#endif

    friend class TaskScheduler;
};

// Formerly ''the_dag''. This class is responsible for maintaining all
// task nodes and ordering constraints among them.
class TaskScheduler
{
private:
    TaskScheduler();

protected:
    virtual ~TaskScheduler();

    // the one true DAG
    COIACCESSAPI2 static TaskScheduler dependency_dag;

    // map of all the unfired events in the system.  once an event is
    // fired it is removed from this map.  it maps an event to the DAG
    // node that represents its outstanding work
    typedef std::map< COIEVENT, TaskNode *> event_map;
    event_map all_the_events;

    // map of all failed events that must be left around forever in case
    // something tries to wait on it in the future
    typedef std::map< COIEVENT, COIRESULT> zombie_map;
    zombie_map failed_events;

    // the list of ready to run dag nodes.  if a node is added that happens
    // to have all of its dependencies already fired, then it is added here
    // so that it can be fired by another mechanism (normally a dependency
    // firing would fire a node)
    typedef std::list<TaskNode *> list_of_dags;
    list_of_dags ready_to_initiate;
    list_of_dags ready_to_complete;


    friend class TaskNode;

public:
    //Runs through the ready_to_initiate list and runs initiate() on task nodes in the list
    bool Initiate_Events();
    //Runs through the ready_to_complete list and runs complete() on task nodes in the list
    bool Complete_Events();
    // a hash table of references to current memory allocated that is used to allow card memory overruns
    // when allocating COI buffers by checking that the required memory may be avilable in the near future.
    std::map<uint64_t, uint64_t> active_memory_usage;

    // a helper typedef to make it easier for clients to use the lock
    typedef _PthreadAutoLock_t AutoLock;

    // get a handle to the one true DAG
    static TaskScheduler &Get()
    {
        return dependency_dag;
    }

    // caller can also hold dag lock since it is recursive
    // add a dag node to the DAG, will add it to all the nodes it depends
    // on and once they are all finished will call its initiate function
    void AddWork(TaskNode *n, int num, const COIEVENT *deps)
    {
        AutoLock al(lock);
        n->add_to_graph(num, deps);

    }

    // caller can also hold dag lock since it is recursive
    // in the case some error happened, then we can remove a previously added
    // node, returning the DAG to its previous state.  this must be done from
    // within the same lock scope as the add otherwise the behaviour is
    // undefined
    void RemoveWork(TaskNode *n, int num, const COIEVENT *deps)
    {
        AutoLock al(lock);
        n->remove_from_graph(num, deps);
    }

    //add a TaskNode just to the map. this is user event
    void AddToMap(TaskNode *n);

    // caller can also hold dag lock since it is recursive
    // every once in a while, a node will be added to the dag that ends up
    // with no actual dependencies outstanding.  this means that nothing will
    // fire which will make the node ever fire.  so we add this function
    // in order to make the system make progress
    TESTIMPORT void RunReady();

    // Caller can also hold dag lock since it is recursive starts the
    // work for a node that has been decided it ready to go
    void Initiate(TaskNode *n);

    // Caller can also hold dag lock since it is recursive called when a node
    // is complete, will initiate any work that is dependent on the completion
    // of this node.  SHOULD NOT be called from a dag node's initiate method.
    TESTIMPORT void Complete(TaskNode *n, COIRESULT r = COI_SUCCESS);

    // This node failed somehow, remember it so that later waits can return the
    // correct error.  SHOULD ONLY be called from the initiate method of a
    // node.
    TESTIMPORT void Failed(TaskNode *n, COIRESULT r);

    //Get the dag node associated with the event
    TaskNode *GetTaskNode(const COIEVENT &e);

    //Set the event UserData
    //This needs to be in the task scheduler as a function, so the mutex
    //can be managed when checking for the existance of the COIEVENT
    COIRESULT SetTaskNodeCallBackData(const COIEVENT &e,
                                      const COI_EVENT_CALLBACK in_Callback,
                                      const void *in_UserData);


    // checks if an event is signaled. locks the dag in order to do it.
    // COI_RETRY means not signaled.  Anything else means it was signaled,
    // possibly with an error.
    TESTIMPORT COIRESULT IsEventSignaled(const COIEVENT &e);

    // Waits forever for an event to be signaled.  takes the lock. COI_RETRY
    // means not signaled.  Anything else means it was signaled, possibly with
    // an error.
    COIRESULT WaitForEvent(const COIEVENT &e);
    // complicated bit of waiting going on here.  takes the lock return true if
    // something was signaled
    //

    //If time out is -1 waits indefinitely to receive a signal.  If time out is
    //+ve waits for a signal for that time Calculates time only if calTime is
    //true wise uses the abstime passed in the function
    int WaitForEvent_signal(
        struct timespec  &abstime,
        const   int32_t  timeout,
        bool Caltime);

    // Returns the lock that protects this data structure during various
    // operations.
    pthread_mutex_t &GetLock()
    {
        return lock;
    }

    int GetEventCount();
private:
    pthread_mutex_t lock;
    pthread_cond_t  cond;
};

// Helper template for automatically removing nodes from the dag
// in the event of errors
template<typename T>
class AutoTaskNode
{
    AutoTaskNode &operator=(const AutoTaskNode &);
    AutoTaskNode(const AutoTaskNode &);
public:
    AutoTaskNode(T *n)
        : node_(n),
          num_(node_->Dependencies()),
          deps_(NULL),
          remove_(false),
          free_(true)
    {
    };
    // automatically remove from dag if not committed
    ~AutoTaskNode()
    {
        if (remove_)
            TaskScheduler::Get().RemoveWork(node_, deps_ ? num_ : 0, deps_);
        if (free_)
            delete node_;
    }
    void AddTask(const COIEVENT *deps)
    {
        deps_ = deps;
        TaskScheduler::Get().AddWork(node_, deps_ ? num_ : 0, deps_);
        free_ = true;
        remove_ = true;
    }
    T *node()
    {
        return node_;
    }
    // allow easy access to original node
    T *operator->()
    {
        return node();
    }
    // do not clean up no destruction
    void commit()
    {
        remove_ = false;
        free_   = false;
    }
private:
    T          *node_;
    const   uint32_t    num_;
    const   COIEVENT   *deps_;
    bool        remove_;
    bool        free_;
};

// a dag node that just depends on N previous nodes
class ChokeNode : public TaskNode
{
public:
    ChokeNode(int num_deps) : TaskNode(num_deps) {};
    virtual bool initiate()
    {
        return true;
    }

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "Choke";
    }
#endif
};

#endif /* _DEPENDENCYDAG_H */
