int createOutputFile(char *fileName, int flags, struct stat *sb)
{
    int fd;

    fd = open(fileName, flags, sb->st_mode);
    if (fd < 0) {
      message(MESS_ERROR, "error creating output file %s: %s\n",
            fileName, strerror(errno));
      return -1;
    }
    if (fchmod(fd, (S_IRUSR | S_IWUSR) & sb->st_mode)) {
      message(MESS_ERROR, "error setting mode of %s: %s\n",
            fileName, strerror(errno));
      close(fd);
      return -1;
    }
    if (fchown(fd, sb->st_uid, sb->st_gid)) {
      message(MESS_ERROR, "error setting owner of %s: %s\n",
            fileName, strerror(errno));
      close(fd);
      return -1;
    }
    if (fchmod(fd, sb->st_mode)) {
      message(MESS_ERROR, "error setting mode of %s: %s\n",
            fileName, strerror(errno));
      close(fd);
      return -1;
    }
    return fd;
}
