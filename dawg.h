/**
*
*       DAWG.H
*
*       Header file for Directed Acyclic Word Graph access
*
*       The format of a DAWG node (8-bit arbitrary data) is:
*
*        31                24 23  22  21                                     0
*       +--------------------+---+---+--+-------------------------------------+
*       |      Letter        | W | N |??|            Node pointer             |
*       +--------------------+---+---+--+-------------------------------------+
*
*      where N flags the last edge in a node and W flags an edge as the
*      end of a word. 21 bits are used to store the node pointer, so the
*      dawg can contain up to 262143 edges. (and ?? is reserved - all code
*      generating dawgs should set this bit to 0 for now)
*
*      The root node of the dawg is at address 1 (because node 0 is reserved
*      for the node with no edges).
*
*      **** PACKED tries do other things, still to be documented!
*
**/

#define TRUE (0==0)
#define FALSE (0!=0)

#define V_END_OF_WORD   23
#define M_END_OF_WORD   (1L << V_END_OF_WORD)
#define V_END_OF_NODE   22                     /* Bit number of N */
#define M_END_OF_NODE   (1L << V_END_OF_NODE)   /* Can be tested for by <0 */
#define V_LETTER        24
#define M_LETTER        0xFF
#define M_NODE_POINTER  0x1FFFFFL     /* Bit mask for node pointer */

#define MAX_CHARS       256
#define MAX_LINE        256

typedef int NODE;
typedef int INDEX;

