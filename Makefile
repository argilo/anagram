#
# Copyright 2019 Clayton Smith (argilo@gmail.com)
#
# This file is part of anagram.
#
# anagram is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# anagram is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with anagram.  If not, see <https://www.gnu.org/licenses/>.
#

default: lambda.zip

dawg: dawg.c dawg.h copyr.h
	gcc -o dawg dawg.c

dict.dwg: dawg
	aspell -d eo dump master | aspell -l eo expand | tr ' ' '\n' | grep "^[abcĉdefgĝhĥijĵklmnoprsŝtuŭvzABCĈDEFGĜHĤIJĴKLMNOPRSŜTUŬVZ'-]*$$" | iconv -f utf-8 -t iso-8859-3 > dict1.txt
	LC_ALL=C sort dict1.txt | uniq > dict2.txt && rm dict1.txt
	./dawg dict2.txt dict && rm dict2.txt

godeps:
	go get github.com/aws/aws-lambda-go/lambda
	go get golang.org/x/text/encoding/charmap

anagram: anagram.go godeps
	go build anagram.go

lambda.zip: anagram dict.dwg
	zip lambda.zip anagram dict.dwg

clean:
	rm -f dawg
	rm -f dict.dwg
	rm -f anagram
	rm -f lambda.zip
