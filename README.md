# SimpleMail Server & Client Implementation

## 1. Concurrency Handling
The server is designed to handle multiple client connections simultaneously using **I/O Multiplexing** via the `select()` system call.

## 2. State Management & Sequence Validation
To enforce the strict ordering of commands required by the protocol (e.g., ensuring a client cannot send a `SUB` command before a `TO` command), I used a Finite State Machine (FSM).
* An array `int state[MAX_USERS]` tracks the current protocol step for every active file descriptor.
* **States include:** `-1` (Connected), `0` (MODE SEND), `1` (FROM), `2` (TO), `3` (SUB), `4` (BODY), `6` (MODE RECV), and `7` (Authenticated).
* Whenever a command is received, the server checks `state[fd]`. If the command does not match the expected next state, the server invokes the `bad_seq()` function, which sends `ERR Bad sequence`, closes the socket, and securely clears the client's memory footprint to prevent attacks.

## 3. Mailbox File ID Management (Persistence)
The assignment requires that deleted mail IDs are **never reused**. To guarantee this even across server restarts, I implemented persistent metadata tracking.
* **The `id_counter.meta` File:** Inside every user's mailbox directory (e.g., `mailboxes/bob/`), the server maintains a hidden `.meta` file containing a single integer: the next available email ID.
* **Startup:** When `load_users_and_mailboxes()` runs, it attempts to read this file. If the file is missing (e.g., on the very first run of server), it scans the directory, finds the highest existing `<id>.txt`, sets `next_id = max_id + 1`, and generates the `.meta` file.
* **Delivery:** Every time an email is successfully delivered to a user, the server increments their `next_id` and overwrites their `id_counter.meta` file.
* **Result:** Even if a user deletes every single email in their folder, the server remembers their numerical sequence upon restart, completely eliminating ID reuse.

## 4. Command Implementations
Data for each active connection is isolated using a custom `client_` struct, which prevents data leaks between simultaneous users.

### SMTP2 (Sending)
* **FROM / TO / SUB:** The server dynamically allocates memory to store the sender's name and subject. Recipients are validated against the loaded `users.txt` file (converted to lowercase for case-insensitivity) and stored in a dynamically allocated Linked List.
* **BODY:** The server reads text until it encounters a single `.` on a new line. It implements **de-stuffing** (removing the first dot if a line begins with `..`) before appending the text to a buffer. Once terminated, it iterates through the recipient linked list, formats the headers (including a native `<time.h>` timestamp), and writes the file.

### SMP (Receiving & Authentication)
* **AUTH:** The server generates a random 8-character alphanumeric `nonce`. The client concatenates `password + nonce`, hashes it using the **DJB2** algorithm, and sends the integer back. The server performs the same hash and compares them. The server tracks `n_auths_failed` and permanently disconnects the client on the 3rd strike.
* **LIST:** Uses `opendir()` and `readdir()` to scan the user's directory. It identifies all `.txt` files, sorts them numerically, opens each to extract the `From:`, `Subject:`, and `Date:` strings, and sends them to the client separated by tabs (`\t`).
* **READ:** The server opens the requested file and sends it line-by-line. To prevent the client from prematurely thinking the message is over, the server implements **dot-stuffing** (prepending a `.` to any line that natively starts with one).
* **DELETE:** Uses the standard C `remove()` function to permanently delete the specified file from the system.

## 5. Assumptions
1. **usernames** given in the **users.txt** file are unique.
2. **username**, **display name**, **password** are non-empty strings.
3. **Meta file** created by server for each registered user will **not** be modified by outside sources. Meta file is crucial for ensuring uniqueness of the mail IDs generated.

## 6. Execution
```
make compile
./smserver 8080 users.txt
```
```
./smclient 127.0.0.1 8080
```