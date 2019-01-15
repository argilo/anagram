/*
Copyright 2019 Clayton Smith (argilo@gmail.com)

This file is part of anagram.

anagram is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

anagram is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with anagram.  If not, see <https://www.gnu.org/licenses/>.
*/

package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"github.com/aws/aws-lambda-go/lambda"
	"golang.org/x/text/encoding/charmap"
	"os"
	"strings"
	"unicode"
)

const MAX_ANAGRAMS = 10000

type Dawg struct {
	encoding *charmap.Charmap
	edges    []uint32
}

type Word struct {
	original string
	vector   []int
	mask     int
}

func readDawg(path string, encoding *charmap.Charmap) Dawg {
	file, _ := os.Open(path)
	defer file.Close()

	buf := make([]byte, 4)
	file.Read(buf)
	numEdges := int(binary.LittleEndian.Uint32(buf))

	edges := make([]uint32, numEdges+1)
	for i := 1; i < numEdges+1; i++ {
		file.Read(buf)
		edges[i] = binary.LittleEndian.Uint32(buf)
	}

	return Dawg{encoding, edges}
}

func buildVector(alphabet, words string) (vector []int, mask, first int) {
	vector = make([]int, len(alphabet))
	first = -1

	for _, char := range words {
		if aIndex := strings.IndexRune(alphabet, unicode.ToLower(char)); aIndex != -1 {
			vector[aIndex]++
			mask |= (1 << uint(aIndex))
			if first == -1 {
				first = aIndex
			}
		}
	}

	return
}

func removeWord(vector []int, word Word) (mask, first int, err error) {
	for i := range vector {
		if word.vector[i] > vector[i] {
			err = errors.New("can't remove word")
			return
		}
	}

	first = -1
	for i := range vector {
		vector[i] -= word.vector[i]
		if vector[i] != 0 {
			mask |= (1 << uint(i))
			if first == -1 {
				first = i
			}
		}
	}

	return
}

func candidates(alphabet string, partialWord string, candidateSets [][]Word, excludes []string, dawg Dawg, dIndex uint32, vector []int, chars, min, max int) {
	const isWord = 1 << 23
	const isLast = 1 << 22
	const next = 0x1fffff

	for {
		edge := dawg.edges[dIndex]
		char := dawg.encoding.DecodeByte(byte(edge >> 24))
		aIndex := strings.IndexRune(alphabet, unicode.ToLower(char))

		if aIndex == -1 || vector[aIndex] != 0 {
			newPartialWord := partialWord + string(char)

			if aIndex != -1 {
				vector[aIndex]--
				chars++
			}

			if (chars >= min) && (chars <= max) && (edge&isWord != 0) {
				wordVector, wordMask, first := buildVector(alphabet, newPartialWord)
				newWord := Word{newPartialWord, wordVector, wordMask}

				excluded := false
				for _, exclude := range excludes {
					if strings.EqualFold(newPartialWord, exclude) {
						excluded = true
						break
					}
				}

				if !excluded {
					candidateSets[first] = append(candidateSets[first], newWord)
				}
			}
			if nextEdge := edge & next; (chars <= max) && (nextEdge != 0) {
				candidates(alphabet, newPartialWord, candidateSets, excludes, dawg, nextEdge, vector, chars, min, max)
			}

			if aIndex != -1 {
				vector[aIndex]++
				chars--
			}
		}

		if edge&isLast != 0 {
			break
		}
		dIndex++
	}
}

func anagrams(accum []Word, candidateSets [][]Word, results *[]string, vector []int, mask, first, depth, maxDepth int) error {
	for _, word := range candidateSets[first] {
		if word.mask&mask != word.mask {
			continue
		}

		newMask, newFirst, err := removeWord(vector, word)
		if err != nil {
			continue
		}

		newAccum := append(accum, word)
		if newMask == 0 {
			result := make([]string, len(newAccum))
			for i, anagramWord := range newAccum {
				result[i] = anagramWord.original
			}
			*results = append(*results, strings.Join(result, " "))
			if len(*results) == MAX_ANAGRAMS {
				return errors.New("reached result limit")
			}
		} else {
			if depth+1 < maxDepth {
				err := anagrams(newAccum, candidateSets, results, vector, newMask, newFirst, depth+1, maxDepth)
				if err != nil {
					return err
				}
			}
		}

		for i := range vector {
			vector[i] += word.vector[i]
		}
	}

	return nil
}

type MyEvent struct {
	Body string `json:"body"`
}

type AnagramParams struct {
	Word       string `json:"vorto"`
	Include    string `json:"inkluzivu"`
	Exclude    string `json:"ekskluzivu"`
	MaxWords   int    `json:"maksvortoj"`
	MaxLetters int    `json:"maksliteroj"`
	MinLetters int    `json:"minliteroj"`
	Candidates bool   `json:"kandidatoj"`
}

type MyResponse struct {
	StatusCode int               `json:"statusCode"`
	Body       string            `json:"body"`
	Headers    map[string]string `json:"headers"`
}

func HandleRequest(ctx context.Context, event MyEvent) (MyResponse, error) {
	var params AnagramParams
	json.Unmarshal([]byte(event.Body), &params)

	encoding := charmap.ISO8859_3
	alphabet := "ĥĵŝhŭzĉĝcbfgvpdmujktrslnoeia"
	candidateSets := make([][]Word, len(alphabet))
	excludes := make([]string, 0)
	if len(params.Exclude) != 0 {
		excludes = strings.Split(params.Exclude, " ")
	}
	dawg := readDawg("dict.dwg", encoding)
	vector, mask, first := buildVector(alphabet, params.Word)

	includeWords := make([]Word, 0)
	if len(params.Include) != 0 {
		for _, include := range strings.Split(params.Include, " ") {
			var err error

			includeVector, includeMask, _ := buildVector(alphabet, include)
			includeWord := Word{include, includeVector, includeMask}
			mask, first, err = removeWord(vector, includeWord)

			if err != nil {
				return MyResponse{400, "", map[string]string{
					"Content-Type":                "text/html",
					"Access-Control-Allow-Origin": "*",
				}}, nil
			}

			includeWords = append(includeWords, includeWord)
		}
	}

	candidates(alphabet, "", candidateSets, excludes, dawg, 1, vector, 0, params.MinLetters, params.MaxLetters)

	results := make([]string, 0)
	if params.Candidates {
	Outer:
		for _, set := range candidateSets {
			for _, result := range set {
				results = append(results, result.original)
				if len(results) == MAX_ANAGRAMS {
					break Outer
				}
			}
		}
	} else {
		anagrams(includeWords, candidateSets, &results, vector, mask, first, len(includeWords), params.MaxWords)
	}

	return MyResponse{200, strings.Join(results, "<br>\n"), map[string]string{
		"Content-Type":                "text/html",
		"Access-Control-Allow-Origin": "*",
	}}, nil
}

func main() {
	lambda.Start(HandleRequest)
}
