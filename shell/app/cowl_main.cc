// Copyright 2026 The COWL Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/app/content_main.h"

#include "cowl/src/cowl_main_delegate.h"

int main(int argc, const char** argv) {
  cowl::CowlMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.argc = argc;
  params.argv = argv;
  return content::ContentMain(std::move(params));
}
