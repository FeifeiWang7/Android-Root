static int do_setlk(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_mapping->host;
	int status;

	/*
	 * Flush all pending writes before doing anything
	 * with locks..
	 */
	status = filemap_fdatawrite(filp->f_mapping);
	if (status == 0) {
		down(&inode->i_sem);
		status = nfs_wb_all(inode);
		up(&inode->i_sem);
		if (status == 0)
			status = filemap_fdatawait(filp->f_mapping);
	}
	if (status < 0)
		return status;

	lock_kernel();
	status = NFS_PROTO(inode)->lock(filp, cmd, fl);
	/* If we were signalled we still need to ensure that
	 * we clean up any state on the server. We therefore
	 * record the lock call as having succeeded in order to
	 * ensure that locks_remove_posix() cleans it out when
	 * the process exits.
	 */
	if (status == -EINTR || status == -ERESTARTSYS)
		posix_lock_file(filp, fl);
	unlock_kernel();
	if (status < 0)
		return status;
	/*
	 * Make sure we clear the cache whenever we try to get the lock.
	 * This makes locking act as a cache coherency point.
	 */
	filemap_fdatawrite(filp->f_mapping);
	down(&inode->i_sem);
	nfs_wb_all(inode);	/* we may have slept */
	up(&inode->i_sem);
	filemap_fdatawait(filp->f_mapping);
	nfs_zap_caches(inode);
	return 0;
}
