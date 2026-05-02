# MKS Todo File Demo

Small file-backed todo CLI written in MKS.

## What it demonstrates

- `entity` as the main object-building mechanism
- simple file persistence through `std.fs`
- small local-app structure without extra tooling

## Run

From the project directory:

```bash
cd MKSTestProjects/todo_file
../../build/mks main.mks
```

It creates and uses:

```text
todos.txt
```

## Commands

```text
add
list
done
remove
clear
quit
```
