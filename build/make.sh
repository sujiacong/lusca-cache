#!/bin/bash

function check_user
{
   local user=`whoami`
   if [[ "$user" != "root" ]]
   then
       echo "netentsec-squid2 need using root account to build package"
       exit 1
   fi
}

RUNDIR=`dirname $0`

if [[ "$RUNDIR" = "." ]]
then
   RUNDIR=`pwd`
else
   RUNDIR=`pwd`/$RUNDIR
fi

cd $RUNDIR/..

chmod 755 configure

mkdir -p /usr/local/nswcf

chmod 755 ./bootstrap.sh 
chmod 755 ./configure

./bootstrap.sh

./configure --prefix=/usr/local/nswcf/squid --enable-async-io --with-pthreads \
--enable-storeio=aufs,coss,null --enable-linux-netfilter \
--enable-arp-acl --enable-epoll --enable-removal-policies=lru,heap \
--enable-snmp --enable-cache-digests \
--enable-referer-log --enable-useragent-log \
--with-aio --with-dl --enable-cache-digests \
--enable-stacktraces --enable-truncate \
--with-large-files --enable-http-violations \
--enable-follow-x-forwarded-for \
--enable-large-cache-files \
--enable-truncate --disable-select --disable-poll \
--disable-unlinkd --disable-dependency-tracking \
--disable-htcp --disable-ident-lookups \
--enable-linux-tproxy4

find . -name Makefile|xargs -I {} sed -i "s/^CFLAGS =.*/CFLAGS = -m64 -Wall -Werror -Wno-unused-function -g -O0 -D_REENTRANT/" {}

make clean

make

make install

chown -R nobody:nobody /usr/local/nswcf/squid/var

tar -czvf /usr/local/nswcf/squid.tgz /usr/local/nswcf/squid
