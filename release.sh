#!/usr/bin/env bash
#
#		Generate the announce template
#
# Completely copy/paste of Xorg/util/modular/release.sh:
#
# Copyright (c) 2009, Oracle and/or its affiliates. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

export LC_ALL=C

#------------------------------------------------------------------------------
#			Function: check_local_changes
#------------------------------------------------------------------------------
#
check_local_changes() {
    git diff --quiet HEAD > /dev/null 2>&1
    if [ $? -ne 0 ]; then
	echo ""
	echo "Uncommitted changes found. Did you forget to commit? Aborting."
	echo ""
	echo "You can perform a 'git stash' to save your local changes and"
	echo "a 'git stash apply' to recover them after the tarball release."
	echo "Make sure to rebuild and run 'make distcheck' again."
	echo ""
	echo "Alternatively, you can clone the module in another directory"
	echo "and run ./configure. No need to build if testing was finished."
	echo ""
	return 1
    fi
    return 0
}

#------------------------------------------------------------------------------
#			Function: check_option_args
#------------------------------------------------------------------------------
#
# perform sanity checks on cmdline args which require arguments
# arguments:
#   $1 - the option being examined
#   $2 - the argument to the option
# returns:
#   if it returns, everything is good
#   otherwise it exit's
check_option_args() {
    option=$1
    arg=$2

    # check for an argument
    if [ x"$arg" = x ]; then
	echo ""
	echo "Error: the '$option' option is missing its required argument."
	echo ""
	usage
	exit 1
    fi

    # does the argument look like an option?
    echo $arg | $GREP "^-" > /dev/null
    if [ $? -eq 0 ]; then
	echo ""
	echo "Error: the argument '$arg' of option '$option' looks like an option itself."
	echo ""
	usage
	exit 1
    fi
}

#------------------------------------------------------------------------------
#			Function: check_modules_specification
#------------------------------------------------------------------------------
#
check_modules_specification() {

if [ x"${INPUT_MODULES}" = x ]; then
    echo ""
    echo "Error: no modules specified (blank command line)."
    usage
    exit 1
fi

}

#------------------------------------------------------------------------------
#			Function: generate_announce
#------------------------------------------------------------------------------
#
generate_announce()
{
    cat <<RELEASE
Subject: [ANNOUNCE] $pkg_name $pkg_version
To: $list_to
Cc: $list_cc

$pkg_name $tag_name is out.

The list of interesting changes are:

See the full log at the end if you are interested in the details.

`git log --no-merges "$tag_range" | git shortlog`

git tag: $tag_name

RELEASE

    for tarball in $tarbz2 $targz $tarxz; do
	cat <<RELEASE
The libratbag project does not generate tarballs for releases, you can
grab one directly from github:

https://github.com/libratbag/$pkg_name/archive/$tag_name/$tarball

RELEASE
    done
}

#------------------------------------------------------------------------------
#			Function: print_epilog
#------------------------------------------------------------------------------
#
print_epilog() {

    epilog="========  Successful Completion"
    if [ x"$NO_QUIT" != x ]; then
	if [ x"$failed_modules" != x ]; then
	    epilog="========  Partial Completion"
	fi
    elif [ x"$failed_modules" != x ]; then
	epilog="========  Stopped on Error"
    fi

    echo ""
    echo "$epilog `date`"

    # Report about modules that failed for one reason or another
    if [ x"$failed_modules" != x ]; then
	echo "	List of failed modules:"
	for mod in $failed_modules; do
	    echo "	$mod"
	done
	echo "========"
	echo ""
    fi
}

#------------------------------------------------------------------------------
#			Function: process_modules
#------------------------------------------------------------------------------
#
# Loop through each module to release
# Exit on error if --no-quit was not specified
#
process_modules() {
    for MODULE_RPATH in ${INPUT_MODULES}; do
	if ! process_module ; then
	    echo "Error: processing module \"$MODULE_RPATH\" failed."
	    failed_modules="$failed_modules $MODULE_RPATH"
	    if [ x"$NO_QUIT" = x ]; then
		print_epilog
		exit 1
	    fi
	fi
    done
}

#------------------------------------------------------------------------------
#			Function: process_module
#------------------------------------------------------------------------------
# Code 'return 0' on success to process the next module
# Code 'return 1' on error to process next module if invoked with --no-quit
#
process_module() {

    top_src=`pwd`
    echo ""
    echo "========  Processing \"$top_src/$MODULE_RPATH\""

    # This is the location where the script has been invoked
    if [ ! -d $MODULE_RPATH ] ; then
	echo "Error: $MODULE_RPATH cannot be found under $top_src."
	return 1
    fi

    # Change directory to be in the git module
    cd $MODULE_RPATH
    if [ $? -ne 0 ]; then
	echo "Error: failed to cd to $MODULE_RPATH."
	return 1
    fi

    # ----- Now in the git module *root* directory ----- #

    # Check that this is indeed a git module
    if [ ! -d .git ]; then
	echo "Error: there is no git module here: `pwd`"
	return 1
    fi

    # Check for uncommitted/queued changes.
    check_local_changes
    if [ $? -ne 0 ]; then
	cd $top_src
	return 1
    fi

    # Determine what is the current branch and the remote name
    current_branch=`git branch | $GREP "\*" | sed -e "s/\* //"`
    remote_name=`git config --get branch.$current_branch.remote`
    remote_branch=`git config --get branch.$current_branch.merge | cut -d'/' -f3,4`
    echo "Info: working off the \"$current_branch\" branch tracking the remote \"$remote_name/$remote_branch\"."

    # Find out the tarname from meson.build
    pkg_name=$($GREP '^project(' meson.build | sed 's|project(\([^\s,]*\).*|\1|' | sed "s/'//g")
    pkg_version=$($GREP -m 1 '^[	 ]*version \?: ' meson.build | sed 's|[	 ]*version \?: \([^\s,]*\).*|\1|' | sed "s/'//g")
    tar_name="$pkg_name-$pkg_version"
    targz=$tar_name.tar.gz
    # libratbag tags with v0.3 but piper user just 0.3
    case `git describe` in
	    v*)
		    tag_name=$(echo "v$pkg_version" | sed 's|\.0$||')
		    ;;
	    *)
		    tag_name=$(echo "$pkg_version" | sed 's|\.0$||')
		    ;;
    esac
    remote_url_root="https://github.com/libratbag/$pkg_name/archive"

    # Now get the tarballs from github directly to compute their checksum
    remote_targz_url=$remote_url_root/$tag_name/$targz
    echo "downloading the tarball from $remote_targz_url"
    curl $remote_targz_url > $targz

    # Obtain the top commit SHA which should be the version bump
    # It should not have been tagged yet (the script will do it later)
    local_top_commit_sha=`git  rev-list --max-count=1 HEAD`
    if [ $? -ne 0 ]; then
	echo "Error: unable to obtain the local top commit id."
	cd $top_src
	return 1
    fi

    # Check that the top commit looks like a version bump
    git diff --unified=0 HEAD^ | $GREP -F $pkg_version >/dev/null 2>&1
    if [ $? -ne 0 ]; then
	# Wayland repos use  m4_define([wayland_major_version], [0])
	git diff --unified=0 HEAD^ | $GREP -E "(major|minor|micro)_version" >/dev/null 2>&1
	if [ $? -ne 0 ]; then
	    echo "Error: the local top commit does not look like a version bump."
	    echo "       the diff does not contain the string \"$pkg_version\"."
	    local_top_commit_descr=`git log --oneline --max-count=1 $local_top_commit_sha`
	    echo "       the local top commit is: \"$local_top_commit_descr\""
	    cd $top_src
	    return 1
	fi
    fi

    # Check that the top commit has been pushed to remote
    remote_top_commit_sha=`git  rev-list --max-count=1 $remote_name/$remote_branch`
    if [ $? -ne 0 ]; then
	echo "Error: unable to obtain top commit from the remote repository."
	cd $top_src
	return 1
    fi
    if [ x"$remote_top_commit_sha" != x"$local_top_commit_sha" ]; then
	echo "Error: the local top commit has not been pushed to the remote."
	local_top_commit_descr=`git log --oneline --max-count=1 $local_top_commit_sha`
	echo "       the local top commit is: \"$local_top_commit_descr\""
	cd $top_src
	return 1
    fi

    # If a tag exists with the the tar name, ensure it is tagging the top commit
    # It may happen if the version set in configure.ac has been previously released
    tagged_commit_sha=`git  rev-list --max-count=1 $tag_name 2>/dev/null`
    if [ $? -eq 0 ]; then
	# Check if the tag is pointing to the top commit
	if [ x"$tagged_commit_sha" != x"$remote_top_commit_sha" ]; then
	    echo "Error: the \"$tag_name\" already exists."
	    echo "       this tag is not tagging the top commit."
	    remote_top_commit_descr=`git log --oneline --max-count=1 $remote_top_commit_sha`
	    echo "       the top commit is: \"$remote_top_commit_descr\""
	    local_tag_commit_descr=`git log --oneline --max-count=1 $tagged_commit_sha`
	    echo "       tag \"$tag_name\" is tagging some other commit: \"$local_tag_commit_descr\""
	    cd $top_src
	    return 1
	else
	    echo "Info: module already tagged with \"$tag_name\"."
	fi
    else
	# Tag the top commit with the tar name
	if [ x"$DRY_RUN" = x ]; then
	    git tag -s -m $tag_name $tag_name
	    if [ $? -ne 0 ]; then
		echo "Error:  unable to tag module with \"$tag_name\"."
		cd $top_src
		return 1
	    else
		echo "Info: module tagged with \"$tag_name\"."
	    fi
	else
	    echo "Info: skipping the commit tagging in dry-run mode."
	fi
    fi

    # Mailing lists where to post the all [Announce] e-mails
    list_to="input-tools@lists.freedesktop.org"

    # Pushing the top commit tag to the remote repository
    if [ x$DRY_RUN = x ]; then
	echo "Info: pushing tag \"$tag_name\" to remote \"$remote_name\":"
	git push $remote_name $tag_name
	if [ $? -ne 0 ]; then
	    echo "Error: unable to push tag \"$tag_name\" to the remote repository."
	    echo "       it is recommended you fix this manually and not run the script again"
	    cd $top_src
	    return 1
	fi
    else
	echo "Info: skipped pushing tag \"$tag_name\" to the remote repository in dry-run mode."
    fi

    # --------- Generate the announce e-mail ------------------
    # Failing to generate the announce is not considered a fatal error

    # Git-describe returns only "the most recent tag", it may not be the expected one
    # However, we only use it for the commit history which will be the same anyway.
    tag_previous=`git describe --abbrev=0 HEAD^ 2>/dev/null`
    # Git fails with rc=128 if no tags can be found prior to HEAD^
    if [ $? -ne 0 ]; then
	if [ $? -ne 0 ]; then
	    echo "Warning: unable to find a previous tag."
	    echo "         perhaps a first release on this branch."
	    echo "         Please check the commit history in the announce."
	fi
    fi
    if [ x"$tag_previous" != x ]; then
	# The top commit may not have been tagged in dry-run mode. Use commit.
	tag_range=$tag_previous..$local_top_commit_sha
    else
	tag_range=$tag_name
    fi
    generate_announce > "$tar_name.announce"
    echo "Info: [ANNOUNCE] template generated in \"$tar_name.announce\" file."
    echo "      Please pgp sign and send it."

    # --------- Successful completion --------------------------
    cd $top_src
    return 0

}

#------------------------------------------------------------------------------
#			Function: usage
#------------------------------------------------------------------------------
# Displays the script usage and exits successfully
#
usage() {
    basename="`expr "//$0" : '.*/\([^/]*\)'`"
    cat <<HELP

Usage: $basename [options] path...

Where "path" is a relative path to a git module, including '.'.

Options:
  --dry-run           Does everything except tagging and uploading tarballs
  --force             Force overwriting an existing release
  --help              Display this help and exit successfully
  --no-quit           Do not quit after error; just print error message

HELP
}

#------------------------------------------------------------------------------
#			Script main line
#------------------------------------------------------------------------------
#

# Choose which grep program to use (on Solaris, must be gnu grep)
if [ "x$GREP" = "x" ] ; then
    if [ -x /usr/gnu/bin/grep ] ; then
	GREP=/usr/gnu/bin/grep
    else
	GREP=grep
    fi
fi

# Process command line args
while [ $# != 0 ]
do
    case $1 in
    # Does everything except uploading tarball
    --dry-run)
	DRY_RUN=yes
	;;
    # Force overwriting an existing release
    # Use only if nothing changed in the git repo
    --force)
	FORCE=yes
	;;
    # Display this help and exit successfully
    --help)
	usage
	exit 0
	;;
    # Do not quit after error; just print error message
    --no-quit)
	NO_QUIT=yes
	;;
    --*)
	echo ""
	echo "Error: unknown option: $1"
	echo ""
	usage
	exit 1
	;;
    -*)
	echo ""
	echo "Error: unknown option: $1"
	echo ""
	usage
	exit 1
	;;
    *)
	INPUT_MODULES="${INPUT_MODULES} $1"
	;;
    esac

    shift
done

# If no modules specified (blank cmd line) display help
check_modules_specification

# Loop through each module to release
# Exit on error if --no-quit no specified
process_modules

# Print the epilog with final status
print_epilog
