/* cd.c, created from cd.def. */
#line 22 "./cd.def"
#include <config.h>

#if defined (HAVE_UNISTD_H)
#  ifdef _MINIX
#    include <sys/types.h>
#  endif
#  include <unistd.h>
#endif

#include "../bashtypes.h"
#include "posixdir.h"
#include "posixstat.h"
#ifndef _MINIX
#include <sys/param.h>
#endif

#include <stdio.h>

#include "../bashansi.h"
#include "../bashintl.h"

#include <errno.h>
#include <tilde/tilde.h>

#include "../shell.h"
#include "../flags.h"
#include "maxpath.h"
#include "common.h"
#include "bashgetopt.h"

#if !defined (errno)
#include <errno.h>
#endif /* !errno */

extern int posixly_correct;
extern int array_needs_making;
extern const char * const bash_getcwd_errstr;

static int bindpwd __P((int));
static void setpwd __P((char *));
static char *resetpwd __P((char *));
static int change_to_directory __P((char *, int));

/* Change this to 1 to get cd spelling correction by default. */
int cdspelling = 0;

int cdable_vars;

#line 97 "./cd.def"

/* Just set $PWD, don't change OLDPWD.  Used by `pwd -P' in posix mode. */
static void
setpwd (dirname)
     char *dirname;
{
  int old_anm;
  SHELL_VAR *tvar;

  old_anm = array_needs_making;
  tvar = bind_variable ("PWD", dirname ? dirname : "", 0);
  if (old_anm == 0 && array_needs_making && exported_p (tvar))
    {
      update_export_env_inplace ("PWD=", 4, dirname ? dirname : "");
      array_needs_making = 0;
    }
}

static int
bindpwd (no_symlinks)
     int no_symlinks;
{
  char *dirname, *pwdvar;
  int old_anm, r;
  SHELL_VAR *tvar;

  r = sh_chkwrite (EXECUTION_SUCCESS);

#define tcwd the_current_working_directory
  dirname = tcwd ? (no_symlinks ? sh_physpath (tcwd, 0) : tcwd)
  		 : get_working_directory ("cd");
#undef tcwd

  old_anm = array_needs_making;
  pwdvar = get_string_value ("PWD");

  tvar = bind_variable ("OLDPWD", pwdvar, 0);
  if (old_anm == 0 && array_needs_making && exported_p (tvar))
    {
      update_export_env_inplace ("OLDPWD=", 7, pwdvar);
      array_needs_making = 0;
    }

  setpwd (dirname);

  if (dirname && dirname != the_current_working_directory)
    free (dirname);

  return (r);
}

/* Call get_working_directory to reset the value of
   the_current_working_directory () */
static char *
resetpwd (caller)
     char *caller;
{
  char *tdir;
      
  FREE (the_current_working_directory);
  the_current_working_directory = (char *)NULL;
  tdir = get_working_directory (caller);
  return (tdir);
}

#define LCD_DOVARS	0x001
#define LCD_DOSPELL	0x002
#define LCD_PRINTPATH	0x004
#define LCD_FREEDIRNAME	0x010

/* This builtin is ultimately the way that all user-visible commands should
   change the current working directory.  It is called by cd_to_string (),
   so the programming interface is simple, and it handles errors and
   restrictions properly. */
int
cd_builtin (list)
     WORD_LIST *list;
{
  char *dirname, *cdpath, *path, *temp;
  int path_index, no_symlinks, opt, lflag;

#if defined (RESTRICTED_SHELL)
  if (restricted)
    {
      sh_restricted ((char *)NULL);
      return (EXECUTION_FAILURE);
    }
#endif /* RESTRICTED_SHELL */

  no_symlinks = no_symbolic_links;
  reset_internal_getopt ();
  while ((opt = internal_getopt (list, "LP")) != -1)
    {
      switch (opt)
	{
	case 'P':
	  no_symlinks = 1;
	  break;
	case 'L':
	  no_symlinks = 0;
	  break;
	default:
	  builtin_usage ();
	  return (EXECUTION_FAILURE);
	}
    }
  list = loptend;

  lflag = (cdable_vars ? LCD_DOVARS : 0) |
	  ((interactive && cdspelling) ? LCD_DOSPELL : 0);

  if (list == 0)
    {
      /* `cd' without arguments is equivalent to `cd $HOME' */
      dirname = get_string_value ("HOME");

      if (dirname == 0)
	{
	  builtin_error (_("HOME not set"));
	  return (EXECUTION_FAILURE);
	}
      lflag = 0;
    }
  else if (list->word->word[0] == '-' && list->word->word[1] == '\0')
    {
      /* This is `cd -', equivalent to `cd $OLDPWD' */
      dirname = get_string_value ("OLDPWD");

      if (dirname == 0)
	{
	  builtin_error (_("OLDPWD not set"));
	  return (EXECUTION_FAILURE);
	}
#if 0
      lflag = interactive ? LCD_PRINTPATH : 0;
#else
      lflag = LCD_PRINTPATH;		/* According to SUSv3 */
#endif
    }
  else if (absolute_pathname (list->word->word))
    dirname = list->word->word;
  else if (privileged_mode == 0 && (cdpath = get_string_value ("CDPATH")))
    {
      dirname = list->word->word;

      /* Find directory in $CDPATH. */
      path_index = 0;
      while (path = extract_colon_unit (cdpath, &path_index))
	{
	  /* OPT is 1 if the path element is non-empty */
	  opt = path[0] != '\0';
	  temp = sh_makepath (path, dirname, MP_DOTILDE);
	  free (path);

	  if (change_to_directory (temp, no_symlinks))
	    {
	      /* POSIX.2 says that if a nonempty directory from CDPATH
		 is used to find the directory to change to, the new
		 directory name is echoed to stdout, whether or not
		 the shell is interactive. */
	      if (opt && (path = no_symlinks ? temp : the_current_working_directory))
		printf ("%s\n", path);

	      free (temp);
#if 0
	      /* Posix.2 says that after using CDPATH, the resultant
		 value of $PWD will not contain `.' or `..'. */
	      return (bindpwd (posixly_correct || no_symlinks));
#else
	      return (bindpwd (no_symlinks));
#endif
	    }
	  else
	    free (temp);
	}

      /* POSIX.2 says that if `.' does not appear in $CDPATH, we don't
	 try the current directory, so we just punt now with an error
	 message if POSIXLY_CORRECT is non-zero.  The check for cdpath[0]
	 is so we don't mistakenly treat a CDPATH value of "" as not
	 specifying the current directory. */
      if (posixly_correct && cdpath[0])
	{
	  builtin_error ("%s: %s", dirname, strerror (ENOENT));
	  return (EXECUTION_FAILURE);
	}
    }
  else
    dirname = list->word->word;

  /* When we get here, DIRNAME is the directory to change to.  If we
     chdir successfully, just return. */
  if (change_to_directory (dirname, no_symlinks))
    {
      if (lflag & LCD_PRINTPATH)
	printf ("%s\n", dirname);
      return (bindpwd (no_symlinks));
    }

  /* If the user requests it, then perhaps this is the name of
     a shell variable, whose value contains the directory to
     change to. */
  if (lflag & LCD_DOVARS)
    {
      temp = get_string_value (dirname);
      if (temp && change_to_directory (temp, no_symlinks))
	{
	  printf ("%s\n", temp);
	  return (bindpwd (no_symlinks));
	}
    }

  /* If the user requests it, try to find a directory name similar in
     spelling to the one requested, in case the user made a simple
     typo.  This is similar to the UNIX 8th and 9th Edition shells. */
  if (lflag & LCD_DOSPELL)
    {
      temp = dirspell (dirname);
      if (temp && change_to_directory (temp, no_symlinks))
	{
	  printf ("%s\n", temp);
	  return (bindpwd (no_symlinks));
	}
      else
	FREE (temp);
    }

  builtin_error ("%s: %s", dirname, strerror (errno));
  return (EXECUTION_FAILURE);
}

#line 344 "./cd.def"

/* Non-zero means that pwd always prints the physical directory, without
   symbolic links. */
static int verbatim_pwd;

/* Print the name of the current working directory. */
int
pwd_builtin (list)
     WORD_LIST *list;
{
  char *directory;
  int opt, pflag;

  verbatim_pwd = no_symbolic_links;
  pflag = 0;
  reset_internal_getopt ();
  while ((opt = internal_getopt (list, "LP")) != -1)
    {
      switch (opt)
	{
	case 'P':
	  verbatim_pwd = pflag = 1;
	  break;
	case 'L':
	  verbatim_pwd = 0;
	  break;
	default:
	  builtin_usage ();
	  return (EXECUTION_FAILURE);
	}
    }
  list = loptend;

#define tcwd the_current_working_directory

  directory = tcwd ? (verbatim_pwd ? sh_physpath (tcwd, 0) : tcwd)
		   : get_working_directory ("pwd");

  /* Try again using getcwd() if canonicalization fails (for instance, if
     the file system has changed state underneath bash). */
  if ((tcwd && directory == 0) ||
      (posixly_correct && same_file (".", tcwd, (struct stat *)0, (struct stat *)0) == 0))
    directory = resetpwd ("pwd");

#undef tcwd

  if (directory)
    {
      printf ("%s\n", directory);
      /* This is dumb but posix-mandated. */
      if (posixly_correct && pflag)
	setpwd (directory);
      if (directory != the_current_working_directory)
	free (directory);
      return (sh_chkwrite (EXECUTION_SUCCESS));
    }
  else
    return (EXECUTION_FAILURE);
}

/* Do the work of changing to the directory NEWDIR.  Handle symbolic
   link following, etc.  This function *must* return with
   the_current_working_directory either set to NULL (in which case
   getcwd() will eventually be called), or set to a string corresponding
   to the working directory.  Return 1 on success, 0 on failure. */

static int
change_to_directory (newdir, nolinks)
     char *newdir;
     int nolinks;
{
  char *t, *tdir;
  int err, canon_failed, r, ndlen, dlen;

  tdir = (char *)NULL;

  if (the_current_working_directory == 0)
    {
      t = get_working_directory ("chdir");
      FREE (t);
    }

  t = make_absolute (newdir, the_current_working_directory);

  /* TDIR is either the canonicalized absolute pathname of NEWDIR
     (nolinks == 0) or the absolute physical pathname of NEWDIR
     (nolinks != 0). */
  tdir = nolinks ? sh_physpath (t, 0)
		 : sh_canonpath (t, PATH_CHECKDOTDOT|PATH_CHECKEXISTS);

  ndlen = strlen (newdir);
  dlen = strlen (t);

  /* Use the canonicalized version of NEWDIR, or, if canonicalization
     failed, use the non-canonical form. */
  canon_failed = 0;
  if (tdir && *tdir)
    free (t);
  else
    {
      FREE (tdir);
      tdir = t;
      canon_failed = 1;
    }

  /* In POSIX mode, if we're resolving symlinks logically and sh_canonpath
     returns NULL (because it checks the path, it will return NULL if the
     resolved path doesn't exist), fail immediately. */
  if (posixly_correct && nolinks == 0 && canon_failed && (errno != ENAMETOOLONG || ndlen > PATH_MAX))
    {
#if defined ENAMETOOLONG
      if (errno != ENOENT && errno != ENAMETOOLONG)
#else
      if (errno != ENOENT)
#endif
	errno = ENOTDIR;
      free (tdir);
      return (0);
    }

  /* If the chdir succeeds, update the_current_working_directory. */
  if (chdir (nolinks ? newdir : tdir) == 0)
    {
      /* If canonicalization failed, but the chdir succeeded, reset the
	 shell's idea of the_current_working_directory. */
      if (canon_failed)
	{
	  t = resetpwd ("cd");
	  if (t == 0)
	    set_working_directory (tdir);
	}
      else
	set_working_directory (tdir);

      free (tdir);
      return (1);
    }

  /* We failed to change to the appropriate directory name.  If we tried
     what the user passed (nolinks != 0), punt now. */
  if (nolinks)
    {
      free (tdir);
      return (0);
    }

  err = errno;

  /* We're not in physical mode (nolinks == 0), but we failed to change to
     the canonicalized directory name (TDIR).  Try what the user passed
     verbatim. If we succeed, reinitialize the_current_working_directory. */
  if (chdir (newdir) == 0)
    {
      t = resetpwd ("cd");
      if (t == 0)
	set_working_directory (tdir);
      else
	free (t);

      r = 1;
    }
  else
    {
      errno = err;
      r = 0;
    }

  free (tdir);
  return r;
}
