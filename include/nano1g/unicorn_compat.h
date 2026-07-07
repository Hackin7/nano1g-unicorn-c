#ifndef NANO1G_UNICORN_COMPAT_H
#define NANO1G_UNICORN_COMPAT_H

#if defined(__has_include)
#  if __has_include(<unicorn/unicorn.h>)
#    include <unicorn/unicorn.h>
#  elif __has_include(<unicorn.h>)
#    include <unicorn.h>
#  else
#    error "Unicorn header not found"
#  endif
#else
#  include <unicorn/unicorn.h>
#endif

#endif
