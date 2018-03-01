#!/usr/local/bin/ksh93 -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD$

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)cache_004_neg.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/tests/cache/cache.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: cache_004_neg
#
# DESCRIPTION:
#	Attaching a cache device fails.
#
# STRATEGY:
#	1. Create pool with separated cache devices.
#	2. Attaching a cache device for existing cache device
#	3. Verify the operation fails
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2008-03-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

log_assert "Attaching a cache device fails."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	log_must $ZPOOL create $TESTPOOL $type $VDEV \
		cache $LDEV

	ldev=$(random_get $LDEV)
	typeset ldev2=$(random_get $LDEV2)
	log_mustnot $ZPOOL attach $TESTPOOL $ldev $ldev2
	log_must check_vdev_state $TESTPOOL $ldev2 ""

	log_must $ZPOOL destroy -f $TESTPOOL
done

log_pass "Attaching a cache device fails."
