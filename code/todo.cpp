#include "platform_todo.h"

/* Example usage:
    t add|a "task [+category] [@context]"
    t addm "Multi line things
            add the things"
    t list|ls [search]
    t pri|p itemnumber priority
    t depri|dp itemnumber
    t replace itemnumber "taskalt"
*/

global_variable char* TodoBasename = "todo.txt";
global_variable char* DoneBasename = "done.txt";

//TODO(chronister): Move up into chr.h?
string 
CatStrings(string String1, string String2)
{
    string Result;
    Result.Length = String1.Length + String2.Length;
    Result.Value = (char*)PlatformAllocMemory(Result.Length);
    CatStrings(String1.Length, String1.Value, String2.Length, String2.Value, Result.Length, Result.Value);
    return Result;
}

string
CopyString(string Source)
{
    string Dest;
    Dest.Length = Source.Length;
    Dest.Value = (char*)PlatformAllocMemory(Dest.Length);
    CopyString(Source.Length, Source.Value, Dest.Length, Dest.Value);
    return Dest;
}

//NOTE(chronister): As the name implies, this assumes your string is null-terminated!
string
LoftCString(char* CString)
{
    string Result;
    Result.Length = StringLength(CString);
    Result.Value = CString;
    return Result;
}

void
FreeString(string* Str)
{
    PlatformFreeMemory(Str->Value);
    Str->Length = 0;
    Str->Value = 0;
}

//NOTE(chronister): This function returns whether or not Filename was found
// in either pwd or user home dir. If it wasn't (returns false), the program
// should probably query if it should be created, and create it.
// Default creation folder should probably be the home dir.
internal string
ConstructLocalFilepath(string Filename)
{
    if (PlatformFileExists(Filename.Value))
    {    
        //TODO(chronister): Do we want to expand the current directory to the full path?
        return Filename;
    }

    // We assume this is a well formed, existing directory
    // that looks like "C:/Users/Steve/" or "/home/steve/"
    string UserDir = PlatformGetUserDir(); 
    string ConstructedPath = CatStrings(UserDir, Filename);
    return ConstructedPath;
}

internal string
ReplaceFilenameInFilepath(string Filepath, string ReplacementName)
{
    char* OnePastLastSlash = Filepath.Value;
    for (char* Scan = Filepath.Value;
        *Scan;
        ++Scan)
    {
        if (*Scan == '/' || *Scan == '\\')
        {
            OnePastLastSlash = Scan + 1;
        }
    }
    if (*OnePastLastSlash == '\0' 
        && !(*(OnePastLastSlash - 1) == '/' || *(OnePastLastSlash - 1) == '\\'))
    {
        OnePastLastSlash = Filepath.Value;
    }
    size_t DirLen = OnePastLastSlash - Filepath.Value;
    string Result;
    Result.Length = (uint32)DirLen + ReplacementName.Length;
    Result.Value = (char*)PlatformAllocMemory(Result.Length);
    CatStrings(DirLen, Filepath.Value, ReplacementName.Length, ReplacementName.Value, Result.Length, Result.Value);
    return Result;
}

internal string
GetTodoFilename()
{
    string TodoStr = LoftCString(TodoBasename);
    return ConstructLocalFilepath(TodoStr);
}

internal string
GetDoneFilename(string TodoFilename)
{
    //TODO(chronister): This should be in the same place as the todo.txt given the same context
    // So it needs a way to know which todo.txt is active and use the same directory as that. 
    string DoneStr = ReplaceFilenameInFilepath(TodoFilename, LoftCString(DoneBasename));
    return DoneStr;
}

inline bool32 
IsValidPriority(char Test)
{
    return (Test >= 'A' && Test <= 'Z');
}

//TODO(chronister): Generalize this out into a generic array sort function
//TODO(chronister): Use something other than a bubble sort?
internal void
SortTodoItemList(uint32 Length, todo_item* List, int32 (*Compare)(todo_item, todo_item))
{
    int32 Swaps = 0;
    uint32 N = Length;
    uint32 NewN = 0;
    do 
    {
        NewN = 0;
        for (uint32 i = 1;
            i < N;
            ++i)
        {
            if (Compare(List[i - 1], List[i]) >= 0)
            {
                Swap(List[i], List[i - 1], todo_item);
                NewN = i;
            }
        }
        N = NewN;
    } while(N > 0);
}

int32
CompareStrings(string A, string B)
{
    int i;
    char* String1 = A.Value;
    char* String2 = B.Value;
    for (i = 0;
        String1[i] && String2[i];
        ++i)
    {
        if (String1[i] > String2[i]) { return 1; }
        if (String1[i] < String2[i]) { return -1; }
    }
    //Check one past the last character, as well, in case they were different lengths
    if (String1[i] > String2[i]) { return 1; }
    if (String1[i] < String2[i]) { return -1; }
    return 0;
}

int32
CompareTodoItemPriority(todo_item A, todo_item B)
{
    if (A.Priority == 0 && B.Priority > 0) { return 1; }
    if (B.Priority == 0 && A.Priority > 0) { return -1; }
    if (A.Priority > B.Priority) { return 1; }
    if (A.Priority < B.Priority) { return -1; }

    if (A.Complete && !B.Complete) { return 1; }  
    if (B.Complete && !A.Complete) { return -1; }  
    
    return CompareStrings(A.Body, B.Body);
}

int32
CompareTodoItemLineNum(todo_item A, todo_item B)
{
    if (A.LineNumber < B.LineNumber) { return -1; }
    if (A.LineNumber > B.LineNumber) { return 1; }
    return 0; 
}


internal int32
GetNumberOfLines(read_file_result File)
{
    uint32 LineCount = 0;
    char* Begin = (char*)File.Contents;
    for (uint32 i = 0;
        i < File.ContentsSize;
        ++i)
    {
        if (*(Begin + i) == '\n') { ++LineCount; }
    }
    //NOTE(chronister): The commented out condition would be if trailing newlines were not supported
    //if (*(Begin + File.ContentsSize - 1) != '\n') 
    //{
    LineCount += 1;
    //}
    return LineCount;
}

internal todo_item
ParseTodoLine(int32 LineNum, string Line)
{
    todo_item Item = {};
    Item.Priority = 0;
    Item.LineNumber = LineNum;
    Item.Body = Line;

    if (Line.Value[0] == '(' && Line.Value[2] == ')' && Line.Value[3] == ' ' && IsValidPriority(Line.Value[1]))
    {
        Item.Priority = Line.Value[1];
        Item.Body.Length -= 4;
        Item.Body.Value += 4;
    }
    if (Line.Value[0] == 'x' && Line.Value[1] == ' ')
    {
        Item.Complete = true;
        Item.Body.Length -= 2;
        Item.Body.Value += 2;
    }

    return Item;
}

internal todo_file
ParseTodoFile(read_file_result File)
{
    todo_file Todo = {0};

    Todo.NumberOfItems = GetNumberOfLines(File);
    Todo.Items = (todo_item*)PlatformAllocMemory(sizeof(todo_item)*Todo.NumberOfItems);

    uint32 LineNum = 0;
    char* Begin = (char*)File.Contents;
    char* Start = Begin;
    for (uint32 i = 0;
        i <= File.ContentsSize;
        ++i)
    {
        char* End = Begin + i;
        if (*End == '\n' || i == (File.ContentsSize))
        {
            
            size_t Length = End - Start;
            Assert(LineNum < Todo.NumberOfItems);
            *End = '\0';
            string Line;
            Line.Length = (uint32)Length;
            Line.Value = Start;

            Todo.Items[LineNum] = ParseTodoLine(LineNum+1, Line);
            
            LineNum += 1;


            Start = End + 1;
        }
    }

    return Todo;
}

internal size_t 
GetItemStringSize(todo_item Item)
{
    size_t ItemLength = Item.Body.Length;

    if (Item.Complete)
    {
        ItemLength += 2;
    }
    else if (IsValidPriority(Item.Priority)) 
    {
        ItemLength += 4;
    }

    //NOTE(chronister): If this method ends up being used somewhere other than file serialization,
    // Make sure its still a good idea to have this here.
    ItemLength+=1; // For the newline.
    return ItemLength;
}

internal void
SerializeTodoItem(todo_item Item, size_t* ResultLength, char* Result)
{
    size_t ItemLength = GetItemStringSize(Item);
    Assert(*ResultLength >= ItemLength);

    if (Item.Complete)
    {
        CatStrings(2, "x ", Item.Body.Length, Item.Body.Value, ItemLength-1, Result);
        CatStrings(ItemLength-1, Result, 1, "\n", ItemLength, Result);
    }
    else if (Item.Priority)
    {
        CatStrings(1, "(", 1, &Item.Priority, 2, Result);
        CatStrings(2, Result, 2, ") ", 4, Result);
        CatStrings(4, Result, Item.Body.Length, Item.Body.Value, ItemLength-1, Result);
        CatStrings(ItemLength-1, Result, 1, "\n", ItemLength, Result);
    }
    else 
    {
        CatStrings(Item.Body.Length, Item.Body.Value, 1, "\n", ItemLength, Result);
    }
}

internal read_file_result
SerializeTodoFile(todo_file Todo)
{
    read_file_result Result = {};
    if (Todo.NumberOfItems > 0)
    {
        SortTodoItemList(Todo.NumberOfItems, Todo.Items, &CompareTodoItemLineNum);

        size_t TotalSize = 0;
        foreach(todo_item, Item, Todo.NumberOfItems, Todo.Items)
        {
            TotalSize += GetItemStringSize(Item);
        }

        Result.ContentsSize = TotalSize;
        Result.Contents = (char*)PlatformAllocMemory(TotalSize);

        size_t RunningSize = 0;
        foreach(todo_item, It, Todo.NumberOfItems, Todo.Items)
        {
            //TODO(chronister): Get a better foreach macro that doesn't require this redefinition
            Item = It;
            size_t ItemLength = GetItemStringSize(Item);
            char* Serial = (char*)PlatformAllocMemory(ItemLength);
            
            SerializeTodoItem(Item, &ItemLength, Serial);

            Assert((RunningSize + ItemLength) <= TotalSize);
            CatStrings(RunningSize, Result.Contents, ItemLength, Serial, TotalSize, Result.Contents);
            RunningSize += ItemLength;
        }

        //Eliminate trailing newlines
        if (Result.Contents[Result.ContentsSize - 1] == '\n')
        {
            Result.Contents[Result.ContentsSize - 1] = 0;
            Result.ContentsSize -= 1;
        }
    }

    return Result;
}

todo_file
GetTodoFile()
{
    string Filename = GetTodoFilename();
    read_file_result Result = PlatformReadEntireFile(Filename.Value);
    todo_file Todo = {};
    if (Result.ContentsSize > 0)
    {
        Todo = ParseTodoFile(Result);   
    }
    Todo.Filename = Filename;
    return Todo;
}

todo_file
GetDoneFile(string TodoFilename)
{
    string Filename = GetDoneFilename(TodoFilename);
    read_file_result Result = PlatformReadEntireFile(Filename.Value);
    todo_file Todo = {0};
    if (Result.ContentsSize > 0)
    {
        Todo = ParseTodoFile(Result);   
    }
    Todo.Filename = Filename;
    return Todo;
}

bool32
SaveTodoFile(todo_file Todo)
{
    read_file_result Serialized = SerializeTodoFile(Todo);
    return PlatformWriteEntireFile(Todo.Filename.Value, Serialized.ContentsSize, Serialized.Contents);
}

bool32
SaveDoneFile(todo_file Done)
{
    read_file_result Serialized = SerializeTodoFile(Done);
    return PlatformWriteEntireFile(Done.Filename.Value, Serialized.ContentsSize, Serialized.Contents);
}

void
ListTodoItems(todo_file Todo)
{
    int32 MaxWidth = Log10(Todo.NumberOfItems) + 1;
    foreach(todo_item, Line, Todo.NumberOfItems, Todo.Items)
    {
        if (Line.Body.Length <= 1) { continue; } // Don't display blank lines

        int32 LineWidth = Log10(Line.LineNumber);
        for (int i = 0;
             i < MaxWidth - LineWidth;
             ++i)
        {
            print(" ");
        }

        if (Line.Complete)
        {
            print("%d: x %s\n", Line.LineNumber, Line.Body.Value);
        }
        else if (Line.Priority) 
        {
            print("%d: (%c) %s\n", Line.LineNumber, Line.Priority, Line.Body.Value);
        }
        else
        {
            print("%d: %s\n", Line.LineNumber, Line.Body.Value);
        }
    }
}
void
ListTodoItems()
{
    todo_file Todo = GetTodoFile();
    SortTodoItemList(Todo.NumberOfItems, Todo.Items, &CompareTodoItemPriority);
    ListTodoItems(Todo);
}

int64
GetTodoItemIndexFromLineNumber(uint32 Number, todo_file Todo)
{
    todo_item* Result = null;
    //TODO(chronister): Make some kind of standard, somewhat efficient
    //  array search
    for (uint32 i = 0;
        i < Todo.NumberOfItems;
        ++i)
    {
        if (Todo.Items[i].LineNumber == Number)
        {
            return i;
        }
    }
    return -1;
}

void
AddTodoItem(todo_item Item)
{
    todo_file Todo = GetTodoFile();
    todo_item* OldItems = Todo.Items;
    todo_item* NewItems = (todo_item*)PlatformAllocMemory((Todo.NumberOfItems + 1)*sizeof(todo_item));
    CopyMemory(NewItems, OldItems, (Todo.NumberOfItems)*sizeof(todo_item));
    PlatformFreeMemory((void*)OldItems);
    Todo.Items = NewItems;
    Item.LineNumber = Todo.NumberOfItems+1;
    Todo.Items[Todo.NumberOfItems] = Item;
    Todo.NumberOfItems += 1;
    if (SaveTodoFile(Todo))
    {
        print("Added \"%s\" to todo.txt on line %d.\n", Item.Body.Value, Item.LineNumber);
    }
}

void
AddTodoItem(char* Line)
{
    string ItemStr;
    ItemStr.Length = StringLength(Line);
    ItemStr.Value = Line;

    todo_item Item = ParseTodoLine(0, ItemStr);
    AddTodoItem(Item);
}

//TODO(chronister): Generalize this out into a generic array remove function
internal bool32
UnorderedRemoveTodoItemFromList(uint32* ListLength, todo_item* List, uint32 ItemIndex)
{
    for (uint32 i = 0;
        i < *ListLength;
        ++i)
    {
        if (i == ItemIndex)
        {
            List[i] = List[*ListLength - 1];
            *ListLength -= 1;
            return true;
        }
    }
    return false;
}

void
RemoveTodoItem(int32 ItemNum)
{
    todo_file Todo = GetTodoFile();

    int64 ItemIndex = GetTodoItemIndexFromLineNumber(ItemNum, Todo);
    if (ItemIndex >= 0)
    {
        todo_item* Item = &Todo.Items[ItemIndex];
        if (UnorderedRemoveTodoItemFromList(&Todo.NumberOfItems, Todo.Items, (uint32)ItemIndex)) 
        {
            if (SaveTodoFile(Todo))
            {
                print("Removed item #%d.\n", Item->LineNumber);
            }
        }
        else
        {
            print("Unable to remove item #%d for some reason. Please debug.\n", Item->LineNumber);
        }
    }
    else
    {
        print("Unable to find item #%d.\n", ItemNum);
    }
}

void
PrioritizeTodoItem(int32 ItemNum, char Priority)
{
    todo_file Todo = GetTodoFile();

    int64 ItemIndex = GetTodoItemIndexFromLineNumber(ItemNum, Todo);
    if (ItemIndex >= 0)
    {  
        todo_item* Item = &Todo.Items[ItemIndex];
        Item->Priority = Priority;

        if (SaveTodoFile(Todo))
        {
            if (Priority)
            {
                print("Set the priority of item #%d to %c.\n", Item->LineNumber, Item->Priority);
            }
            else
            {
                print("Deprioritized item #%d.\n", Item->LineNumber, Item->Priority);
            }
        }
    }
    else
    {
        print("Unable to find item #%d.\n", ItemNum);
    }
}

//TODO(chronister): This is the third time this pattern has appeared! Compress it!
void
SetTodoItemCompletion(int32 ItemNum, bool32 Complete)
{
    todo_file Todo = GetTodoFile();

    int64 ItemIndex = GetTodoItemIndexFromLineNumber(ItemNum, Todo);
    if (ItemIndex >= 0)
    {  
        todo_item* Item = &Todo.Items[ItemIndex];
        Item->Complete = Complete;

        if (SaveTodoFile(Todo))
        {
            print("Completed item #%d.\n", Item->LineNumber, Item->Priority);
        }
    }
    else
    {
        print("Unable to find item #%d.\n", ItemNum);
    }
}

void
EditTodoItem(int32 ItemNum, string NewText)
{
    todo_file Todo = GetTodoFile();

    int64 ItemIndex = GetTodoItemIndexFromLineNumber(ItemNum, Todo);
    if (ItemIndex >= 0)
    {  
        todo_item* Item = &Todo.Items[ItemIndex];
        Item->Body = NewText;

        if (SaveTodoFile(Todo))
        {
            print("Edited item #%d.\n", Item->LineNumber);
        }
    }
    else
    {
        print("Unable to find item #%d.\n", ItemNum);
    }
}

uint32
CountCompletedItems(todo_file Todo)
{
    uint32 Result = 0;
    for (uint32 i = 0;
        i < Todo.NumberOfItems;
        ++i)
    {
        if (Todo.Items[i].Complete) { ++Result; }
    }
    return Result;
}

void 
ArchiveCompletedItems()
{
    todo_file Todo = GetTodoFile();
    todo_file Done = GetDoneFile(Todo.Filename);

    uint32 AllCompletedItems = Done.NumberOfItems + CountCompletedItems(Todo);
    todo_item* CompletedItemList = (todo_item*)PlatformAllocMemory(AllCompletedItems * sizeof(todo_item));

    size_t DoneItemsBytes = Done.NumberOfItems*sizeof(todo_item);

    CopyMemory((void*)CompletedItemList, (void*)Done.Items, DoneItemsBytes);

    uint32 RunningNumCompletedItems = Done.NumberOfItems;
    for (uint32 i = 0;
        i < Todo.NumberOfItems;
        ++i)
    {
        todo_item* Item = &Todo.Items[i];
        if (Item->Complete)
        {
            CompletedItemList[RunningNumCompletedItems] = Todo.Items[i];

            Todo.Items[i] = Todo.Items[Todo.NumberOfItems - 1];
            Todo.NumberOfItems -= 1;
            ++RunningNumCompletedItems;
			--i;
        }
    }
    Assert(RunningNumCompletedItems == AllCompletedItems);
    Done.Items = CompletedItemList;
    Done.NumberOfItems = AllCompletedItems;

    if (SaveDoneFile(Done))
    {
        if (SaveTodoFile(Todo))
        {
            print("Archived the completed items.\n");
        }
    }
}

internal parse_args_result 
ParseArgs(int argc, char* argv[])
{
    parse_args_result Result = {};
    int32 CommandIndex = -1;
    for (int32 i = 1;
        i < argc;
        ++i)
    {
        char* Arg = argv[i];

        if (Arg[0] == '-' || Arg[0] == '/')
        {
            if (Arg[1] == 'n') { Result.Flags |= FLAG_REMOVE_BLANK_LINES; }
            if (Arg[1] == 'h') { Result.Flags |= FLAG_HELP; }

            if (Arg[1] == '-')
            {
                char* Word = (Arg + 2);
                if (CompareStrings(Word, "help")) { Result.Flags |= FLAG_HELP; }
            }
        }
        else
        {
            // Doesn't start with " or /, so it's either a command or an arg to a command
            if (CommandIndex < 0)
            {
                if (CompareStrings(Arg, "ls") 
                 || CompareStrings(Arg, "list")) 
                { 
                    Result.Command = CMD_LIST; 
                }
                else if (CompareStrings(Arg, "a")
                      || CompareStrings(Arg, "add")) 
                { 
                    Result.Command = CMD_ADD; 
                }
                else if (CompareStrings(Arg, "edit")
                      || CompareStrings(Arg, "replace")) 
                { 
                    Result.Command = CMD_EDIT; 
                }
                else if (CompareStrings(Arg, "rm") 
                      || CompareStrings(Arg, "del")
                      || CompareStrings(Arg, "remove"))
                {
                    Result.Command = CMD_REMOVE; 
                }
                else if (CompareStrings(Arg, "p")
                      || CompareStrings(Arg, "pri")
                      || CompareStrings(Arg, "prioritize"))
                {
                    Result.Command = CMD_PRIORITIZE;
                }
                else if (CompareStrings(Arg, "dp")
                      || CompareStrings(Arg, "depri")
                      || CompareStrings(Arg, "deprioritize"))
                {
                    Result.Command = CMD_DEPRIORITIZE;
                }
                else if (CompareStrings(Arg, "do")
                      || CompareStrings(Arg, "did")
                      || CompareStrings(Arg, "complete")
                      || CompareStrings(Arg, "finish"))
                {
                    Result.Command = CMD_COMPLETE;
                }
                else if (CompareStrings(Arg, "ar")
                      || CompareStrings(Arg, "archive"))
                {
                    Result.Command = CMD_ARCHIVE;
                }
                else if (CompareStrings(Arg, "help")
                      || CompareStrings(Arg, "h"))
                {
                    Result.Command = CMD_HELP;
                }
                else
                {
                    Result.Command = CMD_UNKNOWN;
                }

                CommandIndex = i;
            }
            else
            {
                parse_int_result IntegerParse = ParseInteger(StringLength(Arg), Arg);
                if (IntegerParse.Valid)
                {
                    Assert(Result.NumericArgCount < 10);
                    Result.NumericArgs[Result.NumericArgCount] = (int32)IntegerParse.Value;
                    Result.NumericArgCount += 1;
                }
                else
                {
                    //TODO(chronister) Should the string args array be dynamic?
                    Assert(Result.StringArgCount < 10);
                    Result.StringArgs[Result.StringArgCount] = Arg;
                    Result.StringArgCount += 1;
                }
            }
        }
    }

    return Result;
}


internal int 
RunFromArguments(parse_args_result Args)
{
    char* Usage = "Usage: todo action [task_number] [task_description]";
    char* Help = "Usage: todo action [task_number] [task_description]\n"
                 "Keep track of items you need to accomplish, organized by +project and @context\n"
                  "\n"
                  "Actions:\n"
                  "  add|a \"Sleep on large pile of gold +DragonThings @home\"\n"
                  "  archive\n"
                  "  del|rm #[ # # ...]\n"
                  "  depri|dp #[ # # ...]\n"
                  "  do|complete #[ # # ...]\n"
                  "  help\n"
                  "  list|ls\n"
                  "  pri|p # [A-Z]\n"
                  "  replace|edit # \"Sleep on even larger pile of gold +DragonThings @home\"\n";

    switch (Args.Command)
    {
        case CMD_LIST:
        {
            ListTodoItems();
        } break;

        case CMD_ADD:
        {
            foreach(char*, It, Args.StringArgCount, Args.StringArgs)
            {
                AddTodoItem(It);
            }
            if (Args.StringArgCount == 0)
            {
                print("Please specify a valid thing to add.\n");
            }
        } break;

        case CMD_EDIT:
        {
            if (Args.NumericArgCount == 1)
            {
                if (Args.StringArgCount == 1)
                {
                    EditTodoItem(Args.NumericArgs[0], LoftCString(Args.StringArgs[0]));
                }
                else 
                {
                    print("Please supply exactly one string with an edited description.\n");
                }
            }
            else
            {
                print("Please supply exactly one item number to edit.\n");
            }
        } break;

        case CMD_REMOVE:
        {
            foreach(int32, It, Args.NumericArgCount, Args.NumericArgs)
            {
                RemoveTodoItem(It);
            }
            if (Args.NumericArgCount == 0)
            {
                print("Please supply at least one item number to remove.\n");
            }
        } break;

        case CMD_PRIORITIZE:
        {
            if (Args.NumericArgCount == 1)
            {
                if (Args.StringArgCount == 1 && IsValidPriority(*(Args.StringArgs[0])))
                {
                    PrioritizeTodoItem(Args.NumericArgs[0], *(Args.StringArgs[0]));
                }
                else 
                {
                    print("Please supply exactly one item priority (A-Z).\n");
                }
            }
            else
            {
                print("Please supply exactly one item number to prioritize.\n");
            }
        } break;

        case CMD_DEPRIORITIZE:
        {
            foreach(int32, It, Args.NumericArgCount, Args.NumericArgs)
            {
                PrioritizeTodoItem(It, 0);
            }
            if (Args.NumericArgCount == 0)
            {
                print("Please supply at least one item number to deprioritize.\n");
            }
        } break;

        case CMD_COMPLETE:
        {
            foreach(int32, It, Args.NumericArgCount, Args.NumericArgs)
            {
                SetTodoItemCompletion(It, true);
            }
            if (Args.NumericArgCount == 0)
            {
                print("Please supply at least one item number to deprioritize.\n");
            }
        } break;

        case CMD_ARCHIVE:
        {
            ArchiveCompletedItems();
        } break;

        case CMD_HELP:
        {
            print(Help);
        } break;

        default:
        {
            if (Args.Flags & FLAG_HELP)
            {
                print(Help);
            }
            else
            {
                print("%s\n"
                      "Try todo -h for more information.\n", Usage);
            }
        } break;
    }

    return 0;
}
