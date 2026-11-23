/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The main of ngui::testkit_main: GoogleTest's, plus the snapshot arguments
// --update-snapshots, --snapshot-dir= and --snapshot-report-dir=
// (specification 20.5).


#include <gtest/gtest.h>

#include <ngui/test/Snapshot.h>


int
main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	ngui::test::ConsumeSnapshotFlags(argc, argv, ngui::test::Snapshots());
	return RUN_ALL_TESTS();
}
