% CRONTAB(1)
% 
% 20 Nov 2019

NAME
====
crontab - manipulate per-user crontabs (dillon's lightweight cron daemon)

SYNOPSIS
========
**crontab file [-u user]** - replace crontab from file

**crontab - [-u user]** - replace crontab from stdin

**crontab -l [-u user]** - list crontab for user

**crontab -e [-u user]** - edit crontab for user

**crontab -d [-u user]** - delete crontab for user

**crontab -c dir** - specify crontab directory

DESCRIPTION
===========

The **crontab** command manipulates the per-user crontabs. If you are
looking for the format of these files, see "man 5 crontab".

Generally the -e option is used to edit your crontab. **crontab** will use
the editor specified by your EDITOR or VISUAL environment
variable (or /usr/bin/vi) to edit the crontab:

	$ crontab -e

Just as a regular user, the superuser also has his or her own per-user
crontab.

NOTES
=====

**crontab** doesn't provide the kinds of protections that programs like **visudo** do
against syntax errors and simultaneous edits. Errors won't be detected until
**crond** reads the crontab file. What **crontab** does is provide a mechanism for
users who may not themselves have write privileges to the crontab folder
to nonetheless install or edit their crontabs. It also notifies a running crond
daemon of any changes to these files.

Only users who belong to the same group as the **crontab** binary will be able
to install or edit crontabs. However it'll be possible for the superuser to
install crontabs even for users who don't have the privileges to install them
themselves. (Even for users who don't have a login shell.) Only the superuser may use
the -u or -c switches to specify a different user and/or crontab directory.

SEE ALSO
========
**crontab**(5)
**crond**(8)

AUTHORS
=======
Matthew Dillon (dillon@apollo.backplane.com): original developer  
James Pryor (dubiousjim@gmail.com): current developer
