#include "copyr.h"
/*****************************************************************************/
/*                                                                           */
/*      FILE : DAWG.C                                                        */
/*                                                                           */
/*      Convert an alphabetically-ordered dictionary file in text format     */
/*      (one word per line) into a Directed Acyclic Word Graph. The command  */
/*      syntax is                                                            */
/*                                                                           */
/*              DAWG <text file (inc .ext)> <output file (no ext)>           */
/*                                                                           */
/*      The first 4 bytes of the output file form a 24-bit number containing */
/*      the number of edges in the dawg. The rest of the file contains the   */
/*      dawg itself (see "The World's Fastest Scrabble Program" by Appel     */
/*      and Jacobson for a description of a dawg).                           */
/*                                                                           */
/*****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dawg.h"   /* main system constants */


#define EXIT_OK       (0)     /* Generic success              */
#define EXIT_ERROR    (1)     /* Generaic failure             */

#define ABS(x) ((x) < 0 ? -(x) : (x))

/**
*  The following constant, HASH_TABLE_SIZE, can be changed according to
*  dictionary size.  The two values shown will be fine for both large
*  and small systems.  It MUST be prime.
**/

/* pick one about 20% larger than needed */
#define HASH_TABLE_SIZE 240007

/* Suitable prime numbers if you want to tune it:
     30011
    150001  <-- probably a good compromise. OK for dicts to about 1Mb text
    200003
    220009
    240007

   If you try any others, for goodness sake CHECK THAT THEY ARE PRIME!!!

 */
#define MAX_EDGES (HASH_TABLE_SIZE-1)

static FILE *fpin, *fpout;     /* Input/output files */
static char current_word[(MAX_LINE+1)];  /* The last word read from fpin */
static int first_diff, save_first_diff;
                                /* The position of the first letter at which */
                                /* current_word differs from the previous    */
                                /* word read                                 */
static NODE *hash_table;
static int first_time = TRUE;
static int this_char = '?', lastch;
static NODE *dawg;

static int FILE_ENDED = FALSE;  /* I'm having real problems getting
 merged dawgs to work on the PC; this is yet another filthy hack. Sorry. */

static INDEX nwords, nnodes, total_edges;

/*
                Forward references
 */

static INDEX build_node(int depth);
static INDEX add_node(NODE  *edge_list, INDEX nedges);
static void read_next_word(void);
static INDEX compute_hashcode(NODE *edge_list, INDEX nedges);
static void report_size(void);

/**
*       main
*
*       Program entry point
**/

int words; /* dirty communication variable -- the multi-pass stuff
               was hacked in at the last minute. */

int
main(int argc, char **argv)
{
  INDEX i;
  char fname[128];

  if (argc != 3) {
    fprintf(stderr,
      "usage: dawg dictfile.ext dawgfile\n");
    exit(EXIT_ERROR);
  }

  /**
  *  Allocate the memory for the dawg
  **/

  if ((dawg = malloc(MAX_EDGES * sizeof(NODE *))) == NULL) {
    fprintf(stderr, "Can\'t allocate dictionary memory\n");
    exit(EXIT_ERROR);
  }
  for (i = 0; i < (INDEX)MAX_EDGES; i++) dawg[i] = 0L;
  /**
  *  Allocate the hash table.
  *  Fill it with zeroes later just before use.  Don't trust calloc etc.
  *  - not portable enough.  Anyway, in the multi-pass version we don't
  *  want to continually free/claim...
  **/

  if ((hash_table =
      malloc((HASH_TABLE_SIZE+1) * sizeof(NODE))) == NULL) {
    fprintf(stderr, "Can\'t allocate memory for hash table\n");
    exit(EXIT_ERROR);
  }
  /**
  *  Open the input/output files
  **/

  fpin = fopen(argv[1], "r");
  if (fpin == NULL) {
    fprintf(stderr, "Can\'t open text file \"%s\"\n", argv[1]);
    /* Could print out error string but it's not portable... */
    exit(EXIT_ERROR);
  }

  /**
  *  Read the first word from the dictionary
  **/

  first_time = TRUE;
  nwords = 0;
  current_word[0] = 0;
  read_next_word();
  lastch = *current_word;
  /**
  *  Initialise the counters, taking account of the root node (which is
  *  a special case)
  **/

  nnodes = 1; total_edges = MAX_CHARS;

  /**
  *  Build the dawg and report the outcome
  **/

  /* Now, in the dim & distant past, this code supported the concept
     of a restricted character set - ie a..z & A..Z were packed into 6 bits;
     this caused awful problems in the loop below, where we had to try to
     keep the loop-control variable and the character code in synch; nowadays
     chars are 8 bits or else, so I'm starting to tidy up the places
     where these hacks were necessary... */
     
  /* Explicitly initialise hash table to all zeros */
  {INDEX a; for (a = 0; a <= HASH_TABLE_SIZE; a++) hash_table[a] = 0;}
  words = 0;
  (void) build_node(0);

  /**
  *  Save the dawg to file
  **/
  sprintf(fname, "%s.dwg", argv[2]);
  fpout = fopen(fname, "wb");
  if (fpout == NULL) {
    fprintf(stderr, "Can\'t open output file \"%s\"\n", fname);
    exit(EXIT_ERROR);
  }
#ifdef DEBUG
  fprintf(stderr, "Writing to %s\n", fname);
#endif

  *dawg = total_edges;
  total_edges = sizeof(NODE) * (total_edges + 1); /* Convert to byte count */

  {
    int cnt;
    if ((cnt = fwrite(dawg, 1, (int)total_edges, fpout)) != total_edges) {
      fprintf(stderr, "%d bytes written instead of %d\n.", cnt, total_edges);
      exit(EXIT_ERROR);
    }
  }
  fclose(fpout);

  /**
  *  Read the first word from the dictionary
  **/

  first_diff = save_first_diff;
  first_time = FALSE;
  nwords = 0;
  /**
  *  Initialise the counters, taking account of the root node (which is
  *  a special case)
  **/

  nnodes = 1; total_edges = MAX_CHARS;

  lastch = *current_word;
  /**
  *  Build the dawg and report the outcome
  **/

  fclose(fpin);
  fprintf(stderr, "Dawg generated\n");
  exit(EXIT_OK);
}

/**
*       BUILD_NODE
*
*       Recursively build the next node and all its sub-nodes
**/

static INDEX
build_node(int depth)
{
  INDEX nedges = 0;
  INDEX i;
  NODE *edge_list;

  edge_list = NULL;
  if (current_word[depth] == 0) {
    /**
    *  End of word reached. If the next word isn't a continuation of the
    *  current one, then we've reached the bottom of the recursion tree.
    **/

    read_next_word();
    if (first_diff < depth) return(0);
  }

  edge_list = (NODE *)malloc(MAX_CHARS*sizeof(NODE));
                     /* Note this is a 'near' array */
  if (edge_list == NULL) {
    fprintf(stderr, "Stack full (depth %d)\n", depth);
    exit(EXIT_ERROR);
  }
  for (i = 0; i < MAX_CHARS; i++) edge_list[i] = 0L;

  /**
  *  Loop through all the sub-nodes until a word is read which can't
  *  be reached via this node
  **/

  do
    {
    /* Construct the edge. Letter.... */
    edge_list[nedges] = (NODE) (((NODE)current_word[depth]))
                        << (NODE)V_LETTER;
    /* ....end-of-word flag.... */
    if (current_word[depth+1L] == 0) edge_list[nedges] |= M_END_OF_WORD;
    /* ....and node pointer. */
    edge_list[nedges] |= build_node(depth+1); nedges++;
                                                   /* (don't ++ in a macro) */
    } while (first_diff == depth);

  if (first_diff > depth) {
    fprintf(stderr, "Internal error -- first_diff = %d, depth = %d\n",
      first_diff, depth);
    exit(EXIT_ERROR);
  }

  edge_list[nedges-1] |= M_END_OF_NODE;
                                  /* Flag the last edge in the node */

  /**
  *  Add the node to the dawg
  **/

  if (depth) {
    NODE result = add_node(edge_list, nedges);
    free(edge_list);
    return(result);
  }

  /**
  *  depth is zero, so the root node (as a special case) goes at the start
  **/

  edge_list[MAX_CHARS-1] |= M_END_OF_NODE;      /* For backward searches */
  for (i = 0; i < MAX_CHARS; i++)
    {
    dawg[i+1] = edge_list[i];
    }
  free(edge_list);
  return(0);
}

/**
*       ADD_NODE
*
*       Add a node to the dawg if it isn't already there. A hash table is
*       used to speed up the search for an identical node.
**/

static INDEX
add_node(NODE *edge_list, INDEX nedges)
{
  NODE hash_entry;
  INDEX inc;
  INDEX a, first_a;
  INDEX i;

  /**
  *  Look for an identical node. A quadratic probing algorithm is used
  *  to traverse the hash table.
  **/

  first_a = compute_hashcode(edge_list, nedges);
  first_a = ABS(first_a) % HASH_TABLE_SIZE;
  a = first_a;
  inc = 9;

  for (;;)
    {
    hash_entry = hash_table[a] & M_NODE_POINTER;

    if (hash_entry == 0) break;   /* Node not found, so add it to the dawg */

    for (i = 0; i < nedges; i++)
      if (dawg[(hash_entry+i) % HASH_TABLE_SIZE] != edge_list[i]) break;

/* On the 1.6M dictionary, this gave a rangecheck with < 0. Now fixed
   I think - it was a problem with MOD giving unexpected results. */

      if (i == nedges) {
        return(hash_entry);        /* Node found */
      }
      /**
      *  Node not found here. Look in the next spot
      **/

      a += inc;
      inc += 8;
      if (a >= HASH_TABLE_SIZE) a -= HASH_TABLE_SIZE;
      if (inc >= HASH_TABLE_SIZE) inc -= HASH_TABLE_SIZE;
      if (a == first_a) {
        fprintf(stderr, "Hash table full\n");
        exit(EXIT_ERROR);
      }
    }

  /**
  *  Add the node to the dawg
  **/

  if (total_edges + nedges >= MAX_EDGES) {
    fprintf(stderr,
      "Error -- dictionary full - total edges = %d\n", total_edges);
    exit(EXIT_ERROR);
  }

  hash_table[a] |= total_edges + 1;
  nnodes++;
  for (i = 0; i < nedges; i++) {
    dawg[(total_edges + 1 + i) % HASH_TABLE_SIZE] = edge_list[i];
  }
  total_edges += nedges;
  return(total_edges - nedges + 1);
}

/**
*       READ_NEXT_WORD
*
*       Read the next word from the input file, setting first_diff accordingly
**/

static void
read_next_word(void)
{
  /* This stuff imposes the limitation of not allowing '\0' in a word;
     not yet a problem but the dawg structure itself could probably cope
     if the feature were wanted. (Maybe with a little teweaking)       */
  char linebuff[(MAX_LINE+1)];
  int length;
  for (;;)
    {
    int next = 0, c;
    for (;;) {
      c = fgetc(fpin);
      if (FILE_ENDED || c == EOF || ferror(fpin) || feof(fpin)) {
        /* for some reason, we always get a blank line at the end of files */
        current_word[0] = 0;
        first_diff = -1;
        linebuff[next] = '\0';
        FILE_ENDED = TRUE;
        return;
      }
      c &= 255;
      if (next == 0 && c == '\n') continue; /* skip blank lines... */
      linebuff[next++] = c;
      if (c == '\n') break;
    }
    linebuff[next] = '\0';

    words++;

    length = strlen(linebuff);

    if (linebuff[length-1] == '\n') linebuff[length-1] = '\0';
    if (linebuff[length] == '\n') linebuff[length] = '\0';

    if (length < 2 || length > MAX_LINE-1)
      {
      fprintf(stderr, "\n%s - invalid length\n", linebuff);
      continue;    /* Previously exit()ed, but now ignore so that
                      test sites without my pddict can use /usr/dict/words */
      }
    break;
    }
  for (length = 0; current_word[length] == linebuff[length]; length++) {
    /* Get common part of string to check order */
  }
  if ((unsigned char)current_word[length] > (unsigned char)linebuff[length]) {
    fprintf(stderr, "Error -- %s (word out of sequence)\n", linebuff);
    exit(EXIT_ERROR);
  }
  first_diff = length;

  nwords++;
  strcpy(current_word, linebuff);

  if ((nwords > 1) && (!(nwords & 0x3FF))) report_size();
  this_char = current_word[0]; /* for diagnostics... */
}
/**
*       COMPUTE_HASHCODE
*
*       Compute the hash code for a node
**/

static INDEX
compute_hashcode(NODE *edge_list, INDEX nedges)
{
  INDEX i;
  INDEX res = 0L;

  for (i = 0; i < nedges; i++)
    res = ((res << 1) | (res >> 31)) ^ edge_list[i];
  return(res);
}

/**
*       REPORT_SIZE
*
*       Report the current size etc
**/

static void
report_size(void)
{

  if (first_time)
    {
    fprintf(stderr, "      Words    Nodes    Edges    Bytes    BPW\n");
    fprintf(stderr, "      -----    -----    -----    -----    ---\n");
    first_time = FALSE;
    }
  if (*current_word) fprintf(stderr, "%c:", *current_word);
  else fprintf(stderr, "Total:");
  
  /* THE FOLLOWING IS RATHER GRATUITOUS USE OF FLOATING POINT - REMOVE
     IT AND REPLACE WITH INTEGER ARITHMETIC * 100 */

  /* (hey - I already did this in the copy I sent to Richard; so how
      come its missing? Oh no, not again: I've got out of synch and
      used an old copy, haven't I? :-(   ) */ 

  fprintf(stderr, "  %7d  %7d  %7d  %7ld  %5.2f\n",
    nwords, nnodes, total_edges, sizeof(NODE)*(total_edges+1),
      (float)(sizeof(NODE)*(total_edges+1))/(float)nwords);
}
