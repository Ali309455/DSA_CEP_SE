// Command.h - the records stored on the undo / redo stacks.
//
// Every change to the document is made of just two primitive actions
// (Document::insertText and Document::deleteRange), so a Command only needs to
// describe one of those two. The inverse of one is the other:
//
//     Insert  start..end  with text   <->   Delete  start..end  that held text
//
//     undo of an Insert = delete the range start..end
//     undo of a  Delete = insert 'text' back at start
//
// A Group is one step as the user sees it. Usually it holds one Command, but a
// "replace" is a Delete followed by an Insert, and "replace all" is many of
// those; they all sit in ONE group so a single Undo reverses the whole thing.
#pragma once
#include <string>
#include "Document.h"
#include "DynArray.h"

enum class CmdType { Insert, Delete };

struct Command {
    CmdType type = CmdType::Insert;
    Pos start;          // where the change begins
    Pos end;            // where the affected text ends (after an insert / before a delete)
    std::string text;   // the text inserted, or the text that was deleted
};

struct Group {
    std::string label;          // e.g. "insert", "delete word": shown to the user
    DynArray<Command> cmds;     // in the order they were applied
};
