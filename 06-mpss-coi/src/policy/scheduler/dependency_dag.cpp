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

#include <time.h>
    #include <sys/time.h>
#include <errno.h>

#include <internal/_Debug.h>
#include <internal/_DependencyDag.h>
#include <source/COIEvent_source.h>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

    static  __thread bool  t_userDataSet = false;
    static  __thread const void *t_userData    = NULL;

// The one and only task scheduler (dag)
TaskScheduler TaskScheduler::dependency_dag;
// The next event value
// events are really just integers that are stored in a map
// if they are not yet fired
COIEVENT TaskNode::next_event = {{0, (uint64_t)INVALID_EVENT}};
COIEVENT TaskNode::invalid_event = {{(uint64_t)INVALID_EVENT, (uint64_t)INVALID_EVENT}};

// predeclare some helper functions
static void list_remove(node_t *&head, node_t &node);

TaskNode::TaskNode(int num_deps, EVENT_TYPE type)
    : userDataSet(false),
      userData(NULL),
      donated_nodes(new node_t[num_deps]),
      dependents_list(NULL),
      number_of_dependencies(num_deps),
      active_dependencies_left(0)
#ifdef DEBUG
    , magic(MAGIC)
#endif

{
    // assigning a unique COIEVENT
    event.opaque[0] = __sync_fetch_and_add(&(next_event.opaque[0]), 1);
    event.opaque[1] = type;
    if (t_userDataSet)
    {
        userData = t_userData;
        userDataSet = true;
    }
    callBack = NULL;
    initiate_active = false;
}

TaskNode::~TaskNode()
{
#ifdef DEBUG
    magic = 0;
#endif
    assert(active_dependencies_left == 0);
    delete [] donated_nodes;
    donated_nodes = NULL;
}

void
TaskNode::SetUserData(const void *in_UserData)
{
    t_userDataSet = true;
    t_userData = in_UserData;
}

void
TaskNode::ClearUserData()
{
    t_userDataSet = false;
    t_userData = NULL;
}



// Here a node adds itself to the task scheduler (dag).
// For every event that it depends on:
//      it finds the node for that event
//      and adds itself to its list of dependents
// if it turns out that all the events it depends on are already fired
//      adds itself to the ready to run queue
// The TaskScheduler's lock must be held
void TaskNode::add_to_graph(int num, const COIEVENT *dependencies)
{
    assert(num <= number_of_dependencies);

    // get a handle to the ready to run queue
    TaskScheduler::list_of_dags &ready_to_initiate
        = TaskScheduler::Get().ready_to_initiate;

    // and to the map of all outstanding events
    TaskScheduler::event_map &m = TaskScheduler::Get().all_the_events;

    // add itself to the map
    m[this->event] = this;

    DPRINTF("Adding node(%p,%s) with (%d) num_dependencies to the EventMap \n",
            this,
            this->GetNodeType()
            , num);

    // for each dependency
    for (int i = 0; i < num; i++)
    {
        // find the node for the event
        TaskScheduler::event_map::iterator it = m.find(dependencies[i]);

        // if it is not in the map of all events then it is already fired
        if (it != m.end())
        {
            DPRINTF("dependencies(%lu,%lu) Found\n", dependencies[i].opaque[0], dependencies[i].opaque[1]);
            // it was in the map, so add ourself to its list of
            // dependents
            TaskNode *n = it->second;
            active_dependencies_left++;
            // the donated nodes are used to add ourself to another node's
            // list without allocating any new memory in that node
            // each donated node has two pointers, one to the next dependent
            // and one back to the owning node
            donated_nodes[i].self = this;
            donated_nodes[i].event = event;
            donated_nodes[i].next = n->dependents_list;
            n->dependents_list = &(donated_nodes[i]);
        }
    }
    if (0 == active_dependencies_left)
    {
        // if there none of the events we depended on
        // then we are ready to run
        DPRINTF("Adding node(%p,%s) to the ready_to_initiate \n", this, this->GetNodeType());
        ready_to_initiate.push_back(this);
    }
}

// here a node can remove itself from the dag.
// this is a roll back method.  it can only be safely done within the
// same lock scope as the corresponding add_to_graph
// basically
//      lock the dag
//
//      a->add_to_graph;
//      b->add_to_graph;
//      hit error condition
//      b->remove_from_graph
//      a->remove_from_graph
//
//      unlock the dag
// TaskScheduler's lock must be held
void TaskNode::remove_from_graph(int num, const COIEVENT *dependencies)
{
    TaskScheduler::list_of_dags &ready_to_initiate
        = TaskScheduler::Get().ready_to_initiate;
    if (0 == active_dependencies_left)
    {
        // num is the number of oustanding events we were waiting on
        // if it is 0 then we were added to the ready to run queue
        // so we must remove ourselves from it
        TaskScheduler::list_of_dags::iterator it = ready_to_initiate.begin();
        while (it != ready_to_initiate.end())
        {
            if (*it == this)
            {
                ready_to_initiate.erase(it);
                break;
            }
            it++;
        }
    }
    // get a handle to the map
    TaskScheduler::event_map &m = TaskScheduler::Get().all_the_events;

    for (int i = 0; i < num; i++)
    {
        // find each node that we are dependent on
        TaskScheduler::event_map::iterator it = m.find(dependencies[i]);
        if (it != m.end())
        {
            TaskNode *d = it->second;
            // and remove ourselves from its list of dependents
            list_remove(d->dependents_list, donated_nodes[i]);
        }
    }
    active_dependencies_left = 0;
    m.erase(this->event);
}

// complete a dag node entry
// update all dependent nodes
void TaskNode::complete()
{
    base_complete_impl(true);
}

void TaskNode::base_complete_impl(bool callback)
{
    //This check is needed to prevent all threads from erasing a TaskNode
    //while another thread is trying to finish the
    //initiate() function.
    while (1)
    {
        if (!this->initiate_active)
        {
            break;
        }
        DPRINTF("Complete called while tasknode %p is in initiate\n", this);
    }
    TaskScheduler::Get().all_the_events.erase(this->event);

    node_t *p = dependents_list;

    // walk the list of nodes that are dependent on this one
    while (p)
    {
        node_t *next = p->next;

        TaskNode *dependent = p->self;
        assert(dependent);
#ifdef DEBUG
        assert(dependent->magic == MAGIC);
#endif

        dependent->active_dependencies_left--;
        // if this dependent node has reached a number of 0
        // then it is now ready to fire
        DPRINTF("(%p,%s)->active_dep_left: %d\n", dependent, dependent->GetNodeType(), dependent->active_dependencies_left);
        if (0 == dependent->active_dependencies_left)
        {
            DPRINTF("Calling initiate on (%p,%s)\n", dependent, dependent->GetNodeType());

            // Now, we call that node's initiate function.
            // In order to allow other functions to access the DAG while
            // this dependent task node is in initiate we want to
            // unlock the DAG lock, then relock the DAG upon completion.
            dependent->initiate_active = true;
            pthread_mutex_unlock(&TaskScheduler::Get().GetLock());
            bool result = dependent->initiate();
            dependent->initiate_active = false;
            pthread_mutex_lock(&TaskScheduler::Get().GetLock());

            if (result)
            {
                // if that returns true then the node is completely done
                // call its complete function
                dependent->complete();
                // NOTE:
                //  there is no need to remove this node (or any node)
                //  from our list in this function because we are being
                //  destroyed at this point and so our list is useless anyway
                // and delete it
                delete dependent;
                dependent = NULL;
            }
        }
        p = next;
    }
    // Now that we are done we can check to see if the user has any callbacks set
    if (callback)
    {
        do_callback();
    }
}

void TaskNode::do_callback()
{
    if (callBack)
    {
        callBack(event, COI_SUCCESS, userData);
    }
}

TaskScheduler::TaskScheduler()
{
    // Create a recursive mutex so that one component
    // that is in the middle of adding work to the dag
    // can call another component that might also add
    // some work to the dag
    int result = 0;
    pthread_mutexattr_t attr;
    result = pthread_mutexattr_init(&attr);
    if (0 != result) throw errno;

    result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (0 != result) throw errno;

    result = pthread_mutex_init(&lock, &attr);
    if (0 != result) throw errno;

    result = pthread_mutexattr_destroy(&attr);
    if (0 != result) throw errno;

    result = pthread_cond_init(&cond, NULL);
    if (0 != result) throw errno;
}

TaskScheduler::~TaskScheduler()
{
    PT_ASSERT(pthread_mutex_destroy(&lock));
    PT_ASSERT(pthread_cond_destroy(&cond));
}

bool TaskScheduler::Initiate_Events()
{
    bool result = false;
    while (1)
    {
        TaskNode *n = NULL;
        //Scoping the Autolock
        {
            AutoLock al(lock);
            if (!ready_to_initiate.empty())
            {
                n = ready_to_initiate.front();
                ready_to_initiate.pop_front();
            }
            else
            {
                break;
            }
        }

        //With more parallel threads it is possible for this list to get stale
        //such that we could attempt to double signal an event.
        //This check ensures that we cannot have this corner case
        if (IsEventSignaled(n->event) != COI_SUCCESS)
        {
            DPRINTF("ready_to_initiate.front() = (%p,%s)\n", n, n->GetNodeType());
            if (n->initiate_wrapper())
            {
                DPRINTF("Intiate returned true\n");
                //Scoping the Autolock
                {
                    AutoLock al(lock);
                    n->complete();
                    delete n;
                    n = NULL;
                }
                result = true;
            }
        }
    }
    return result;

}

bool TaskScheduler::Complete_Events()
{
    bool result = false;
    while (1)
    {
        TaskNode *n = NULL;
        //Scoping the Autolock
        {
            AutoLock al(lock);
            if (!ready_to_complete.empty())
            {
                n = ready_to_complete.front();
                ready_to_complete.pop_front();
            }
            else
            {
                break;
            }
        }

        //With more parallel threads it is possible for this list ot get stale
        //such that we could attempt to double signal an event.
        //This check ensures that we cannot have this corner case
        if (IsEventSignaled(n->event) != COI_SUCCESS)
        {
            DPRINTF("ready_to_complete.front() = (%p,%s)\n", n, n->GetNodeType());
            //Scoping the Autolock
            {
                AutoLock al(lock);
                n->complete();
                delete n;
                n = NULL;
            }
            result = true;
        }
    }
    return result;
}

TESTIMPORT void TaskScheduler::RunReady()
{

    bool something_completed = false;

    something_completed = Initiate_Events();
    if (something_completed)
    {
        Complete_Events();
    }
    else
    {
        something_completed = Complete_Events();
    }

    if (something_completed)
    {
        // signal the condition variable to wake up any threads
        // waiting on a COIEVENT

        //Scoping Autolock for condition signalling
        {
            AutoLock al(lock);
            PT_ASSERT(pthread_cond_broadcast(&cond));
        }
    }
}

void TaskScheduler::Complete(TaskNode *n, COIRESULT r)
{
    AutoLock al(lock);

    if (COI_SUCCESS != r)
    {
        failed_events[n->GetEvent()] = r;
    }

    ready_to_complete.push_back(n);
}
void TaskScheduler::Initiate(TaskNode *n)
{
    AutoLock al(lock);
    ready_to_initiate.push_back(n);
}

//Needed for error condition. Code Coverage reports it
//not being used but cannot remove it ( because used
//during error condition )
TESTIMPORT void TaskScheduler::Failed(TaskNode *n, COIRESULT r)
{
    AutoLock al(lock);
    TaskScheduler::list_of_dags &ready_to_initiate
        = TaskScheduler::Get().ready_to_initiate;
    TaskScheduler::list_of_dags &ready_to_complete
        = TaskScheduler::Get().ready_to_complete;
    failed_events[n->GetEvent()] = r;
    node_t *p = n->dependents_list;

    // walk the list of nodes that are dependent on this one
    while (p)
    {
        // We have failed a dependency event so we need to remove this event
        // from the running lists initiate and complete along with erasing this
        // event from the dag. The only existence of this event should be
        // within the Failed_events list.
        node_t *next = p->next;
        if (all_the_events.find(p->event) == all_the_events.end())
        {
            // Attempt to delete non-existing node, skipping.
            // It is possible that node have shared dependents
            // and during failure recovery dependant may be already clean
            // by other node that failed.
            // Consider: A->C B->C. A then B failed - B tries to double-clean C.
            p = next;
            continue;
        }
        TaskNode *dependent = p->self;
        assert(dependent);
#ifdef DEBUG
        assert(dependent->magic == MAGIC);
#endif
        TaskScheduler::list_of_dags::iterator init_it = ready_to_initiate.begin();
        while (init_it != ready_to_initiate.end())
        {
            if (*init_it == dependent)
            {
                ready_to_initiate.erase(init_it);
                break;
            }
            init_it++;
        }
        TaskScheduler::list_of_dags::iterator complete_it = ready_to_complete.begin();
        while (complete_it != ready_to_complete.end())
        {
            if (*complete_it == dependent)
            {
                ready_to_complete.erase(complete_it);
                break;
            }
            complete_it++;
        }
        dependent->active_dependencies_left = 0;
        failed_events[dependent->GetEvent()] = r;
        all_the_events.erase(dependent->GetEvent());
        // Recursively call Failed() to clear out all events that depended on
        // this event.
        Failed(dependent, r);
        delete dependent;
        p = next;
    }
}

// If an event is not in the map, then it is signaled and done forever
COIRESULT
TaskScheduler::IsEventSignaled(const COIEVENT &e)
{
    AutoLock al(lock);
    TaskScheduler::event_map::iterator it = all_the_events.find(e);

    if (it != all_the_events.end())
    {
        // Check that the type matches
        if (it->first.opaque[1] != e.opaque[1])
        {
            return COI_SUCCESS;
        }

        return COI_RETRY;
    }
    if (failed_events.end() != failed_events.find(e))
    {
        return failed_events[e];
    }
    return COI_SUCCESS;
}

// blocks on the dag's condition variable, and when fired checks
// if the event is no longer in the map, if it is then try again
// otherwise return indicating that the event is signaled
COIRESULT
TaskScheduler::WaitForEvent(const COIEVENT &e)
{
    AutoLock al(lock);

    event_map &m = all_the_events;

    while (m.find(e) != m.end())
    {
        PT_ASSERT(pthread_cond_wait(&cond, &lock));
    }

    if (failed_events.end() != failed_events.find(e))
    {
        return failed_events[e];
    }
    return COI_SUCCESS;
}

// waits for many events at once with a time out option
// and an option to wait for any or all of them
int TaskScheduler::WaitForEvent_signal(
    struct timespec  &abstime,
    const   int32_t  timeout,
    bool Caltime)
{
    int         retCode = -1;
    // if timeout is > 0 then we need to figure out the absolute
    // system time to set for the timeout
    if (Caltime)
    {
        struct timeval tv;
        // get the current time

        gettimeofday(&tv, NULL);

        // convert to the timespec thing
        abstime.tv_sec = tv.tv_sec;
        abstime.tv_nsec = tv.tv_usec * 1000;

        // and now add in the timeout value
        // pthread_cond_timewait takes an absolute time value
        abstime.tv_sec  +=  timeout / 1000;
        abstime.tv_nsec += (timeout % 1000) * 1000000;
        if (abstime.tv_nsec >= 1000000000)
        {
            //if went over 1 million nanoseconds then bump seconds
            //and reduce the nano seconds
            abstime.tv_sec  ++;
            abstime.tv_nsec -= 1000000000;
        }
    }
    if (timeout == - 1) //wait forever
    {

        retCode = pthread_cond_wait(&cond, &lock);

    }
    else if (timeout > 0)// wait until the absolute time (or a signal)
    {
        retCode = pthread_cond_timedwait(&cond, &lock, &abstime);
    }
    else if (timeout == 0)
    {
        assert(0); //don't pass 0 timeout
    }
    return retCode;
}

//Add the event to the map
void TaskScheduler::AddToMap(TaskNode *n)
{
    AutoLock al(lock);
    all_the_events[n->event] = n;
}

//Return task node associated with the event
TaskNode *TaskScheduler::GetTaskNode(const COIEVENT &e)
{
    AutoLock al(lock);
    TaskScheduler::event_map::iterator it = all_the_events.find(e);
    if (it != all_the_events.end())
    {
        return all_the_events[e];
    }
    else
    {
        return NULL;
    }
}

COIRESULT
TaskScheduler::SetTaskNodeCallBackData(const COIEVENT &e,
                                       const COI_EVENT_CALLBACK in_Callback,
                                       const void *in_UserData)
{
    AutoLock al(lock);
    TaskScheduler::event_map::iterator it = all_the_events.find(e);
    if (it != all_the_events.end())
    {
        TaskNode *n = all_the_events[e];
        n->callBack = in_Callback;
        if (in_UserData)
        {
            n->userDataSet = true;
            n->userData = in_UserData;
        }
        return COI_SUCCESS;
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}




// a couple of helper functions to make adding and removing from the
// dependency lists easier

static void list_remove(node_t *&head, node_t &node)
{
    assert(head);

    if (head == &node)
    {
        head = node.next;
    }
    else
    {
        node_t *p = head;
        while (p && p->next != &node)
        {
            p = p->next;
        }
        assert(p);
        p->next = node.next;
    }
}

static bool operator <(const COIEVENT &left, const COIEVENT &right)
{
    return left.opaque[0] < right.opaque[0];
}


int TaskScheduler::GetEventCount()
{
    return (int) all_the_events.size();
}
