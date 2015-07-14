pipe_read_open(struct inode *inode, struct file *filp)
{
	int ret = -ENOENT;

	mutex_lock(&inode->i_mutex);

	if (inode->i_pipe) {
		ret = 0;
		inode->i_pipe->readers++;
	}

	mutex_unlock(&inode->i_mutex);

	return ret;
}

static int
pipe_write_open(struct inode *inode, struct file *filp)
{
	int ret = -ENOENT;

	mutex_lock(&inode->i_mutex);

	if (inode->i_pipe) {
		ret = 0;
		inode->i_pipe->writers++;
	}

	mutex_unlock(&inode->i_mutex);

	return ret;
}

static int
pipe_rdwr_open(struct inode *inode, struct file *filp)
{
	int ret = -ENOENT;

	mutex_lock(&inode->i_mutex);

	if (inode->i_pipe) {
		ret = 0;
		if (filp->f_mode & FMODE_READ)
			inode->i_pipe->readers++;
		if (filp->f_mode & FMODE_WRITE)
			inode->i_pipe->writers++;
	}

	mutex_unlock(&inode->i_mutex);

	return ret;
}
