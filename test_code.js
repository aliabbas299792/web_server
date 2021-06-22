// Various test functions for websockets and other stuff maybe
function test_websocket_wait_recv(){
  x = new WebSocket("wss://radio.erewhon.xyz/ws");
  x.onmessage = (e) => e.data.text().then((data) => console.log(data))
  return x;
}

function test_websocket_send(){
  x = new WebSocket("wss://radio.erewhon.xyz/ws");
  x.onmessage = (e) => e.data.text().then((data) => console.log(data))
  x.onopen = () => x.send("Test string");
}