#include <cstring>

#include "task.h"
#include <TFE_Memory/chunkedArray.h>
#include <TFE_DarkForces/time.h>
#include <TFE_System/system.h>
#include <TFE_Game/igame.h>
#include <TFE_System/profiler.h>
#include <TFE_Settings/settings.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <TFE_Input/replay.h>
#include <stdarg.h>
#include <tuple>
#include <vector>

using namespace TFE_DarkForces;
using namespace TFE_Memory;

// #define TASK_DEBUG 1

enum TaskConstants
{
	TASK_MAX_LEVELS = 16,	// Maximum number of recursion levels.
	TASK_INIT_LEVEL = -1,
};

struct TaskContext
{
	s32 ip[TASK_MAX_LEVELS];				// Current instruction pointer (IP) for each level of recursion.
	TaskFunc callstack[TASK_MAX_LEVELS];	// Funcion pointer for each level of recursion.

	u8* stackMem;		// starts out null, can point to the block if ever needed.
	u32 stackOffset;	// where in the stack we are.

	u8* stackPtr[TASK_MAX_LEVELS];
	u32 stackSize[TASK_MAX_LEVELS];
	u8  delayedCall[TASK_MAX_LEVELS];

	s32 level;
	s32 callLevel;
};

#ifdef TASK_DEBUG
#define TASK_MSG(...) TFE_System::logWrite(LOG_MSG, "TASK", __VA_ARGS__)
#else
#define TASK_MSG(...)
#endif

struct Task
{
	char name[32];

	Task* prev;
	Task* next;
	Task* subtaskNext;
	Task* subtaskParent;	// For secondary tasks, this is the parent they are attached to.
	Task* retTask;			// Task to return to once this one is completed or paused.
	void* userData;
	JBool framebreak;		// JTRUE if the task loop should end after this task.
	
	// Used in place of stack memory.
	TaskContext context;
	TaskFunc localRunFunc;

	// Timing.
	Tick nextTick;
};

namespace TFE_Jedi
{
	enum
	{
		TASK_CHUNK_SIZE = 256,
		TASK_PREALLOCATED_CHUNKS = 1,

		TASK_STACK_SIZE = 32 * 1024,	// 32KB of stack memory.
		TASK_STACK_CHUNK_SIZE = 64,		// 2MB of memory for 64 tasks with stack memory.
	};

	ChunkedArray* s_tasks = nullptr;
	ChunkedArray* s_stackBlocks = nullptr;
	s32 s_taskCount = 0;

	Task  s_rootTask;
	Task* s_taskIter = nullptr;
	Task* s_curTask = nullptr;
	MessageType s_currentMsg = MSG_FREE_TASK;

	TaskContext* s_curContext = nullptr;

	static f64 s_prevTime = 0.0;
	static f64 s_minIntervalInSec = 0.0;
	static s32 s_frameActiveTaskCount = 0;
	static JBool s_taskSystemPaused = JFALSE;
	static bool s_enableTimeLimiter = true;
	static Task* s_taskPauseTask = nullptr;

	void selectNextTask();

	void createRootTask()
	{
		s_tasks = createChunkedArray(sizeof(Task), TASK_CHUNK_SIZE, TASK_PREALLOCATED_CHUNKS, s_gameRegion);
		s_stackBlocks = createChunkedArray(TASK_STACK_SIZE, TASK_STACK_CHUNK_SIZE, TASK_PREALLOCATED_CHUNKS, s_gameRegion);

		s_rootTask = { 0 };
		s_rootTask.prev = &s_rootTask;
		s_rootTask.next = &s_rootTask;
		s_rootTask.nextTick = TASK_SLEEP;

		s_taskIter = &s_rootTask;
		s_curTask = &s_rootTask;
		s_taskCount = 0;
		s_frameActiveTaskCount = 0;

		CVAR_BOOL(s_enableTimeLimiter, "d_enableTaskTimeLimiter", CVFLAG_DO_NOT_SERIALIZE, "Enable the task time limiter.");
	}

	Task* createSubTask(const char* name, TaskFunc func, TaskFunc localRunFunc)
	{
		if (!s_tasks)
		{
			createRootTask();
		}

		Task* newTask = (Task*)allocFromChunkedArray(s_tasks);
		assert(newTask);

		s_taskCount++;
		strcpy(newTask->name, name);

		// Insert newTask at the head of the subtask list in the current "mainline" task.
		newTask->next = s_curTask->subtaskNext;
		newTask->prev = nullptr;
		if (s_curTask->subtaskNext)
		{
			s_curTask->subtaskNext->prev = newTask;
		}
		s_curTask->subtaskNext = newTask;

		// The subtask parent is the current task.
		newTask->subtaskParent = s_curTask;
		// This task has no subtasks.
		newTask->subtaskNext = nullptr;
		newTask->retTask = nullptr;
		newTask->userData = nullptr;
		newTask->framebreak = JFALSE;
		
		newTask->nextTick = 0;

		newTask->context = { 0 };
		newTask->context.callstack[0] = func;
		newTask->localRunFunc = localRunFunc;
		newTask->context.level = TASK_INIT_LEVEL;
		return newTask;
	}

	Task* createTask(const char* name, TaskFunc func, JBool framebreak, TaskFunc localRunFunc)
	{
		if (!s_tasks)
		{
			createRootTask();
		}

		Task* newTask = (Task*)allocFromChunkedArray(s_tasks);
		assert(newTask);

		s_taskCount++;
		// Insert the task after 's_taskIter'
		strcpy(newTask->name, name);
		newTask->next = s_taskIter->next;
		// This was missing?
		if (s_taskIter->next)
		{
			s_taskIter->next->prev = newTask;
		}
		newTask->prev = s_taskIter;
		s_taskIter->next = newTask;
		
		newTask->subtaskNext = nullptr;
		newTask->subtaskParent = nullptr;
		newTask->retTask = nullptr;
		newTask->userData = nullptr;
		newTask->framebreak = framebreak;

		newTask->context = { 0 };
		newTask->context.callstack[0] = func;
		newTask->localRunFunc = localRunFunc;
		newTask->context.level = TASK_INIT_LEVEL;
		newTask->nextTick = s_curTick;

		return newTask;
	}
	
	void task_serializeState(Stream* stream, Task* task, void* userData, LocalMemorySerCallback localMemCallback, bool resetIP)
	{
		SERIALIZE(SaveVersionInit, task->context.ip[0], 0);
		SERIALIZE(SaveVersionInit, task->context.stackSize[0], 0);
		SERIALIZE(SaveVersionInit, task->nextTick, 0);
		if (serialization_getMode() == SMODE_READ && !task->context.stackMem)
		{
			task->context.stackMem = (u8*)allocFromChunkedArray(s_stackBlocks);
			memset(task->context.stackMem, 0, sizeof(u32) * TASK_MAX_LEVELS);
			task->context.stackOffset = 0;
			task->context.stackPtr[0] = task->context.stackMem;
		}
		SERIALIZE(SaveVersionInit, task->context.stackOffset, 0);
		if (localMemCallback)
		{
			localMemCallback(stream, userData, task->context.stackPtr[0]);
		}
		else
		{
			// This only works with relocatable state.
			SERIALIZE_BUF(SaveVersionInit, task->context.stackPtr[0], task->context.stackSize[0]);
		}

		if (resetIP)
		{
			task->context.ip[0] = 0;
		}
	}

	Task* task_getCurrent()
	{
		return s_curTask;
	}

	void task_pause(JBool pause, Task* pauseRunTask)
	{
		s_taskSystemPaused = pause;
		s_taskPauseTask = pauseRunTask;
	}

	void task_free(Task* task)
	{
		// Select the next task before deletion (to avoid executing the same task again).
		if (task == s_curTask)
		{
			selectNextTask();
		}
		// Then remove the task.
		if (task->prev)
		{
			task->prev->next = task->next;
		}
		if (task->next)
		{
			task->next->prev = task->prev;
		}
		// Handle deleting sub-tasks.
		if (task->subtaskNext)
		{
			// Should we free subtasks?
			assert(0);
		}
		// If this task was the subtaskNext, then move the next subtask into that role.
		Task* parent = task->subtaskParent;
		if (parent && parent->subtaskNext == task)
		{
			parent->subtaskNext = task->next;
		}
		
		// Free any memory allocated for the local context.
		freeToChunkedArray(s_stackBlocks, task->context.stackMem);
		// Finally free the task itself from the chunked array.
		freeToChunkedArray(s_tasks, task);
		s_taskCount--;
	}

	void task_reset()
	{
		s_rootTask = { 0 };
		s_rootTask.prev = &s_rootTask;
		s_rootTask.next = &s_rootTask;
		s_rootTask.nextTick = TASK_SLEEP;

		s_taskIter = &s_rootTask;
		s_curTask = &s_rootTask;
		s_taskCount = 0;
		s_frameActiveTaskCount = 0;

		s_taskSystemPaused = JFALSE;
		s_taskPauseTask = nullptr;
	}

	void task_freeAll()
	{
		chunkedArrayClear(s_tasks);
		chunkedArrayClear(s_stackBlocks);

		s_curTask    = nullptr;
		s_curContext = nullptr;
		s_taskCount  = 0;
	}

	void task_shutdown()
	{
		freeChunkedArray(s_tasks);
		freeChunkedArray(s_stackBlocks);

		s_curTask     = nullptr;
		s_taskIter    = nullptr;
		s_tasks       = nullptr;
		s_stackBlocks = nullptr;
		s_curContext  = nullptr;
		s_taskCount   = 0;

		s_prevTime = 0.0;
		s_minIntervalInSec = 0.0;
		s_frameActiveTaskCount = 0;
		s_taskSystemPaused = JFALSE;
		s_taskPauseTask = nullptr;
	}

	void task_makeActive(Task* task)
	{
		task->nextTick = 0;
	}

	void task_setNextTick(Task* task, Tick tick)
	{
		task->nextTick = tick;
	}

	void task_setUserData(Task* task, void* data)
	{
		if (!task) { return; }
		task->userData = data;
	}

	void* task_getUserData()
	{
		return s_curTask ? s_curTask->userData : nullptr;
	}

	const char* task_getName(Task* task)
	{
		return task->name;
	}

	void task_setMessage(MessageType msg)
	{
		s_currentMsg = msg;
	}

	void task_runLocal(Task* task, MessageType msg)
	{
		if (task->localRunFunc)
		{
			Task* prevCur = s_curTask;
			s_curTask = task;

			task->localRunFunc(msg);

			s_curTask = prevCur;
		}
	}

	bool task_hasLocalMsgFunc(Task* task)
	{
		return task->localRunFunc != nullptr;
	}

	void ctxReturn()
	{
		TASK_MSG("Return from function, task: '%s'.", s_curTask->name);

		s32 level = 0;
		if (s_curContext)
		{
			level = s_curContext->level;
			assert(s_curContext->level >= 0 && s_curContext->level < TASK_MAX_LEVELS);
			if (level <= 0 || !s_curContext->delayedCall[level-1])
			{
				s_curContext->level--;
			}
			else
			{
				// We need to reduce by 2 levels since a delayed call does not immediately return to the calling function.
				s_curContext->level -= 2;
			}
			assert(s_curContext->level >= -1 && s_curContext->level < TASK_MAX_LEVELS);

			if (s_curContext->callLevel > 0) { s_curContext->callLevel--; }
		}

		if (level == 0 && s_curTask)
		{
			task_free(s_curTask);
		}
		else if (s_curContext && s_curContext->stackPtr[level])
		{
			// Return the stack memory allocated for this level.
			s_curContext->stackOffset -= s_curContext->stackSize[level];
			assert(s_curContext->stackOffset >= 0 && s_curContext->stackOffset < TASK_STACK_SIZE);
			s_curContext->stackPtr[level] = nullptr;
			s_curContext->stackSize[level] = 0;
		}
	}

	void selectNextTask()
	{
		// Find the next task to run.
		Task* task = s_curTask;
		while (1)
		{
			//////////////////////////////////////////////////////////////////////////////////////////
			// Execution:
			//  * Go to the next task
			//  * Check to see if there are sub-tasks
			//  * If so, the assign current to the sub-task.
			//  * Execute the task.
			//  * Once we are on the last sub-task, then go back to the parent.
			//  * Once the parent executes, then we move on to parent->next and start all over.
			if (task->next)
			{
				// Move the to the next task.
				task = task->next;
				// If the task has sub-tasks, loop until we find the last one.
				// This is usually only one deep.
				while (task->subtaskNext)
				{
					task = task->subtaskNext;
				}
				// Then execute the task.
				if (task->nextTick <= s_curTick || task->framebreak)
				{
					s_currentMsg = MSG_RUN_TASK;
					s_curTask = task;
					return;
				}
			}
			else if (task->subtaskParent)
			{
				// Otherwise, try to execute the parent.
				task = task->subtaskParent;
				if (task->nextTick <= s_curTick || task->framebreak)
				{
					s_currentMsg = MSG_RUN_TASK;
					s_curTask = task;
					return;
				}
			}
			else
			{
				break;
			}
		}

		// If no selection is possible, assign the first task.
		if (!s_curTask && s_taskCount)
		{
			s_curTask = (Task*)chunkedArrayGet(s_tasks, 0);
		}
	}

	void itask_run(Task* task, MessageType msg)
	{
		Task* retTask = s_curTask;

		task->retTask = s_curTask;
		s_currentMsg = msg;
		s_curTask = task;

		TASK_MSG("Run task '%s' with msg: '%d'.", task->name, s_currentMsg);
		s_curContext = &s_curTask->context;
		s32 level = max(0, s_curContext->level + 1);
		TaskFunc runFunc = s_curContext->callstack[level];
		assert(runFunc);
		if (runFunc)
		{
			runFunc(s_currentMsg);
		}
		if (retTask != s_curTask)
		{
			TFE_System::logWrite(LOG_WARNING, "Task", "Correction required upon returning for task_runAndReturn.");
			s_curTask = retTask;
			s_curContext = &s_curTask->context;
		}
		TASK_MSG("Return to task '%s'.", s_curTask->name);
	}

	void itask_yield(Tick delay, s32 ip)
	{
		// This is caused by a bug in a mod.
		// TODO: Figure out the root cause.
		if (s_curContext != &s_curTask->context)
		{
			s_curContext = &s_curTask->context;
		}

		// Copy the ip so we know where to return.
		assert(s_curContext->level >= 0 && s_curContext->level < TASK_MAX_LEVELS);
		s_curContext->ip[s_curContext->level] = ip;
		s_curContext->level--;

		TASK_MSG("Task yield: '%s' for %u ticks", s_curTask->name, delay);

		// If there is a return task, then take it next.
		if (s_curTask->retTask)
		{
			// Clear out the return task once it is executed.
			Task* retTask = s_curTask->retTask;
			s_curTask->retTask = nullptr;

			// Set the next task.
			s_currentMsg = MSG_RUN_TASK;
			s_curTask = retTask;
			s_curContext = &s_curTask->context;

			TASK_MSG("Return Task: '%s'", s_curTask->name);
			return;
		}

		// Update the current tick based on the delay.
		s_curTask->nextTick = (delay < TASK_SLEEP) ? s_curTick + delay : delay;
		
		// Find the next task to run.
		selectNextTask();
		assert(s_curTask);
		TASK_MSG("Task Selected: '%s'", s_curTask->name);
	}
		
	void task_setMinStepInterval(f64 minIntervalInSec)
	{
		s_prevTime = TFE_System::getTime();
		s_minIntervalInSec = minIntervalInSec;
	}

	JBool task_canRun()
	{
		bool timeLimiter = s_enableTimeLimiter;
		if (s_taskCount && timeLimiter)
		{
			const f64 time = TFE_System::getTime();
			if (time - s_prevTime < s_minIntervalInSec)
			{
				return JFALSE;
			}
		}
		return JTRUE;
	}

	void task_updateTime()
	{
		s_prevTime = TFE_System::getTime();
	}

	// Called once per frame to run all of the tasks.
	// Returns JFALSE if it cannot be run due to the time interval.
	JBool task_run()
	{
		if (!s_taskCount)
		{
			return JTRUE;
		}
		TFE_ZONE("Task System");

		// Limit the update rate by the minimum interval.
		// Dark Forces uses discrete 'ticks' to track time and the game behavior is very odd with 0 tick frames.
		const f64 time = TFE_System::getTime();
		const bool timeLimiter = s_enableTimeLimiter && !TFE_Input::isReplaySystemLive();
		if (time - s_prevTime < s_minIntervalInSec && timeLimiter)
		{
			return JFALSE;
		}

		s_prevTime = time;
		s_currentMsg = MSG_RUN_TASK;
		s_frameActiveTaskCount = 0;

		// Return if the task system is paused.
		if (s_taskSystemPaused)
		{
			if (s_taskPauseTask)
			{
				s_curTask = s_taskPauseTask;
				if (s_curTask->nextTick <= s_curTick)
				{
					s_frameActiveTaskCount++;

					s_curContext = &s_curTask->context;
					s32 level = max(0, s_curContext->level + 1);
					TaskFunc runFunc = s_curContext->callstack[level];
					assert(runFunc);

					if (runFunc)
					{
						runFunc(s_currentMsg);
					}
				}
			}
			return JTRUE;
		}

		// Keep processing tasks until the "framebreak" task is hit.
		// Once the framebreak task completes (if it is not sleeping), then break out of the loop - processing will resume
		// on the next task on the next frame.
		// Note: the original code just loop here forever, but we break it up between frames to play nice with modern operating systems.
		while (s_curTask)
		{
			TASK_MSG("Current Task: '%s'.", s_curTask->name);
			JBool framebreak = s_curTask->framebreak;

			// This should only be false when hitting the "framebreak" task which is sleeping.
			if (s_curTask->nextTick <= s_curTick)
			{
				s_frameActiveTaskCount++;

				s_curContext = &s_curTask->context;
				s32 level = max(0, s_curContext->level + 1);
				TaskFunc runFunc = s_curContext->callstack[level];
				assert(runFunc);

				if (runFunc)
				{
					runFunc(s_currentMsg);
				}
			}
			else
			{
				selectNextTask();
			}

			if (framebreak)
			{
				break;
			}
		}
		return JTRUE;
	}

	void task_setDefaults()
	{
		s_curTask  = &s_rootTask;
		s_taskIter = &s_rootTask;

		TFE_COUNTER(s_taskCount, "Task Count");
		TFE_COUNTER(s_frameActiveTaskCount, "Active Tasks");
	}

	s32 task_getCount()
	{
		return s_taskCount;
	}

	s32 ctxGetIP()
	{
		assert(s_curContext->level >= 0 && s_curContext->level < TASK_MAX_LEVELS);
		return s_curContext->ip[s_curContext->level];
	}

	void ctxAllocate(u32 size)
	{
		if (!size) { return; }
		if (!s_curContext->stackMem)
		{
			s_curContext->stackMem = (u8*)allocFromChunkedArray(s_stackBlocks);
			memset(s_curContext->stackSize, 0, sizeof(u32) * TASK_MAX_LEVELS);
			s_curContext->stackOffset = 0;
		}

		s32 level = s_curContext->level;
		if (!s_curContext->stackPtr[level])
		{
			s_curContext->stackPtr[level] = s_curContext->stackMem + s_curContext->stackOffset;
			s_curContext->stackSize[level] = size;
			s_curContext->stackOffset += size;
			assert(s_curContext->stackOffset >= 0 && s_curContext->stackOffset < TASK_STACK_SIZE);

			// Clear out the memory.
			memset(s_curContext->stackPtr[level], 0, size);
		}
	}

	void* ctxGet()
	{
		return s_curContext->stackPtr[s_curContext->level];
	}

	void ctxBegin()
	{
		s_curContext->level++;
	}

	// Direct calls from a task are a bit complicated, especially when those calls can yield.
	// This task needs to track the current IP at the calling level so it can be resumed when the new function returns.
	// In addition, we must detect when the return is delayed - due to a yield in the called function - 
	// so that the recursion level is properly handled on delayed return.
	bool ctxCall(TaskFunc func, MessageType msg, s32 ip, const char* funcName)
	{
		TASK_MSG("Call Function: '%s', %d.", funcName, msg);

		assert(s_curContext->level >= 0 && s_curContext->level + 1 < TASK_MAX_LEVELS);
		TaskContext* startContext = s_curContext;
		if (s_curContext->level == 0)
		{
			s_curContext->callLevel = 0;
		}
		s32 startLevel = s_curContext->level;
		s32 startCallLevel = s_curContext->callLevel;
		startContext->delayedCall[startLevel] = 0;
		s_curContext->callLevel++;

		s_curContext->ip[s_curContext->level] = ip;
		s_curContext->callstack[s_curContext->level + 1] = func;
		s_curContext->ip[s_curContext->level + 1] = 0;
		func(msg);

		if (startCallLevel != startContext->callLevel)
		{
			startContext->delayedCall[startLevel] = 1;
		}
		return startCallLevel != startContext->callLevel;
	}
}