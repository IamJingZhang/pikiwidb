// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
    Responsible for defining the text form of the PikiwiDB logo,
    the output of this LOGO must be combined with the definition
    in practice.
 */

#pragma once

const char* pikiwidbLogo =
    "\n______   _  _     _            _ ______ ______  \n"
    "| ___ \\ (_)| |   (_)          (_)|  _  \\| ___ \\ \n"
    "| |_/ /  _ | | __ _ __      __ _ | | | || |_/ /  PikiwiDB(%s) %d bits \n"  // version and
    "|  __/  | || |/ /| |\\ \\ /\\ / /| || | | || ___ \\ \n"
    "| |     | ||   < | | \\ V  V / | || |/ / | |_/ /  Port: %d\n"
    "\\_|     |_||_|\\_\\|_|  \\_/\\_/  |_||___/  \\____/   https://github.com/OpenAtomFoundation/pikiwidb \n\n\n";
