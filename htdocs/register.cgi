#!/usr/bin/python
#coding:utf-8

import sys,os
length = os.getenv('CONTENT_LENGTH')
if length:
    postdata = sys.stdin.read(int(length))
    print '<html>'
    print '<head>'
    print '<title>POST</title>'
    print '</head>'
    print '<body>'
    print '<h2> POST data </h2>'
    print '<ul>'
    for data in postdata.split('&'):
        print  '<li>'+data+'</li>'
    print '</ul>'
    print '</body>'
    print '</html>'
else:
    print 'no found'