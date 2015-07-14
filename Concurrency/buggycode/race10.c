static int
ptrace_start(long pid, long request,
	     struct task_struct **childp,
	     struct utrace_attached_engine **enginep,
	     struct ptrace_state **statep)

{
	struct task_struct *child;
	struct utrace_attached_engine *engine;
	struct ptrace_state *state;
	int ret;

	NO_LOCKS;

	if (request == PTRACE_TRACEME)
		return ptrace_traceme();

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	pr_debug("ptrace pid %ld => %p\n", pid, child);
	if (!child)
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}

	rcu_read_lock();
	engine = utrace_attach(child, UTRACE_ATTACH_MATCH_OPS,
			       &ptrace_utrace_ops, NULL);
	ret = -ESRCH;
	if (IS_ERR(engine) || engine == NULL)
		goto out_tsk_rcu;
	state = rcu_dereference(engine->data);
	if (state == NULL || state->parent != current)
		goto out_tsk_rcu;
	/*
	 * Traditional ptrace behavior demands that the target already be
	 * quiescent, but not dead.
	 */
	if (request != PTRACE_KILL
	    && !(engine->flags & UTRACE_ACTION_QUIESCE)) {
		/*
		 * If it's in job control stop, turn it into proper quiescence.
		 */
		struct sighand_struct *sighand;
		unsigned long flags;
		sighand = lock_task_sighand(child, &flags);
		if (likely(sighand != NULL)) {
			if (child->state == TASK_STOPPED)
				ret = 0;
			unlock_task_sighand(child, &flags);
		}
		if (ret == 0) {
			ret = ptrace_update(child, state,
					    UTRACE_ACTION_QUIESCE, 0);
			if (unlikely(ret == -EALREADY))
				ret = -ESRCH;
			if (unlikely(ret))
				BUG_ON(ret != -ESRCH);
		}

		if (ret) {
			pr_debug("%d not stopped (%lu)\n",
				 child->pid, child->state);
			goto out_tsk_rcu;
		}

		ret = -ESRCH;  /* Return value for exit_state bail-out.  */
	}

	atomic_inc(&state->refcnt);
	rcu_read_unlock();

	NO_LOCKS;

	/*
	 * We do this for all requests to match traditional ptrace behavior.
	 * If the machine state synchronization done at context switch time
	 * includes e.g. writing back to user memory, we want to make sure
	 * that has finished before a PTRACE_PEEKDATA can fetch the results.
	 * On most machines, only regset data is affected by context switch
	 * and calling utrace_regset later on will take care of that, so
	 * this is superfluous.
	 *
	 * To do this purely in utrace terms, we could do:
	 *  (void) utrace_regset(child, engine, utrace_native_view(child), 0);
	 */
	if (request != PTRACE_KILL) {
		wait_task_inactive(child);
		while (child->state != TASK_TRACED && child->state != TASK_STOPPED) {
			if (child->exit_state) {
				__ptrace_state_free(state);
				goto out_tsk;
			}

			task_lock(child);
			if (child->mm && child->mm->core_waiters) {
				task_unlock(child);
				__ptrace_state_free(state);
				goto out_tsk;
			}
			task_unlock(child);

			/*
			 * This is a dismal kludge, but it only comes up on ia64.
			 * It might be blocked inside regset->writeback() called
			 * from ptrace_report(), when it's on its way to quiescing
			 * in TASK_TRACED real soon now.  We actually need that
			 * writeback call to have finished, before a PTRACE_PEEKDATA
			 * here, for example.  So keep waiting until it's really there.
			 */
			yield();
			wait_task_inactive(child);
		}
	}
	wait_task_inactive(child);

	*childp = child;
	*enginep = engine;
	*statep = state;
	return -EIO;

out_tsk_rcu:
	rcu_read_unlock();
out_tsk:
	NO_LOCKS;
	put_task_struct(child);
out:
	return ret;
}
