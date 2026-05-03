# MKS ToDo TUI

A Vim-like terminal user interface for managing tasks, written in MKS.

## Features

- ✨ Vim-like keyboard controls (j/k navigation)
- 📝 Add, edit, and delete tasks
- ✓ Mark tasks as done/undone
- 🔍 Search and filter tasks
- 💾 Auto-save to file (tasks.txt)
- 📱 Responsive terminal layout
- ⌨️  No mouse required

## Running

### From source:
```bash
mks packages/todo-tui/src/main.mks
```

### From package:
```bash
mks run todo-tui
```

## Keyboard Controls

### Navigation
- `j` or `Down` - move cursor down
- `k` or `Up` - move cursor up

### Task Operations
- `a` - add new task
- `e` - edit selected task
- `d` - delete selected task
- `Space` - toggle task done/undone

### Other Commands
- `/` - search tasks
- `s` - save tasks to file
- `?` - show help
- `q` - quit

## Task Format

Tasks are stored in `tasks.txt` with a simple format:

```
0|Buy groceries
1|Write documentation
0|Fix bug in parser
```

Where:
- `0` = task is not done
- `1` = task is done

## Design Highlights

- **Pure MKS implementation** - no C extensions needed
- **std.tty integration** - uses terminal control library
- **std.fs integration** - persistent storage
- **Error recovery** - graceful handling of terminal resize/signals
- **Responsive UI** - fast rendering, no flicker

## Known Limitations

- Unicode support depends on terminal emulator
- Very long task names may wrap unpredictably
- Search is case-sensitive (current version)

## Future Improvements

- Priority levels for tasks
- Due dates and timestamps
- Categories/tags
- Undo/redo support
- Configurable keybindings
- Export to JSON/CSV
