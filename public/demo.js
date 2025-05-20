const labels = [0,1,2,3,4,5,6,8];
let data = {
labels: labels,
datasets: [{
    label: 'signal',
    data: [65, 59, 80, 81, 56, 55, 40,45],
    fill: false,
    borderColor: 'rgb(75, 192, 192)',
    tension: 0.1
}]
};

function startConnect(){
    clientID = "clientID - "+parseInt(Math.random() * 100);
    host = document.getElementById("host").value;   
    port = document.getElementById("port").value;  
    userId  = document.getElementById("username").value;  
    passwordId = document.getElementById("password").value;  
    document.getElementById("messages").innerHTML += "<span> Connecting to " + host + " on port " +port+" </span><br>";
    document.getElementById("messages").innerHTML += "<span> Using the client Id " + clientID +" </span><br>";
    client = new Paho.Client(host,Number(port),clientID);
    client.onConnectionLost = onConnectionLost;
    client.onMessageArrived = onMessageArrived;
    client.connect({
        onSuccess: onConnect,
        userName: userId,
        password: passwordId
    });
    
}

function onConnect(){
    topic =  document.getElementById("topic_s").value;
    document.getElementById("messages").innerHTML += "<span> Subscribing to topic "+topic + "</span><br>";
    client.subscribe(topic);
}
function onConnectionLost(responseObject){
    document.getElementById("messages").innerHTML += "<span> ERROR: Connection is lost.</span><br>";
    if(responseObject !=0){
        document.getElementById("messages").innerHTML += "<span> ERROR:"+ responseObject.errorMessage +"</span><br>";
    }
}
function onMessageArrived(message){
    //console.log("OnMessageArrived: "+message.payloadString);
    //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName+" | Message : "+message.payloadString + "</span><br>";
    if(message.destinationName=="/settings/tx"){
        document.getElementById("set_frequency_tx").innerHTML=message.payloadString;
        //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName+" | Message : "+message.payloadString + "</span><br>";
        
    }
    if(message.destinationName=="/settings/rx"){
        document.getElementById("set_frequency_rx").innerHTML=message.payloadString;
        //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName+" | Message : "+message.payloadString + "</span><br>";
    }
    if(message.destinationName=="/settings/paris_speed"){
        document.getElementById("set_paris_speed").innerHTML=message.payloadString;
        //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName+" | Message : "+message.payloadString + "</span><br>";
    }
    if(message.destinationName=="/settings/tone"){
        document.getElementById("set_tone_freq").innerHTML=message.payloadString;
        //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName+" | Message : "+message.payloadString + "</span><br>";
    }
    if(message.destinationName=="/settings/space"){
        document.getElementById("set_space").innerHTML=message.payloadString;
        //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName+" | Message : "+message.payloadString + "</span><br>";
    }
    if(message.destinationName=="/data/msg_out"){
        d = new Date();
        datetext = d.toTimeString();
        datetext = datetext.split(' ')[0];

        document.getElementById("rec_msg").innerHTML +="New recieved Message("+datetext+"):"+message.payloadString+ "</span><br>";

    }
    
    /*
    if(message.destinationName=="/data/out"){
        //document.getElementById("messages").innerHTML += "<span> Topic:"+message.destinationName + "</span><br>";
        var arr = [];
        for(let i=0;i<256;i++){
            
            console.log(message.payloadString.slice(5*i,5*i+5).charCodeAt(1));
            var num = (message.payloadString.slice(5*i,5*i+5).charCodeAt(1)-48)*1000+(message.payloadString.slice(5*i,5*i+5).charCodeAt(2)-48)*100+(message.payloadString.slice(5*i,5*i+5).charCodeAt(3)-48)*10+message.payloadString.slice(5*i,5*i+5).charCodeAt(4)-48;
            if(message.payloadString.slice(5*i,5*i+1)=='-')
            {
                num = -num;
            }
            arr.push(num);
        
        
        }
        console.log(message.payloadString);
        console.log(arr);
        
        var new_data = [];
        for(let i=0;i<256;i++){

            new_data.push(i+1);
        }
        console.log(new_data);
        chart_ref.data.labels = new_data;
        chart_ref.data.datasets[0].data = arr;
        chart_ref.update('none');
        
    }*/
}
function startDisconnect(){
    client.disconnect();
    document.getElementById("messages").innerHTML += "<span> Disconnected. </span><br>";
}
function publishMessage(){
msg = document.getElementById("Message").value;
topic = document.getElementById("topic_p").value;
Message = new Paho.Message(msg);
Message.destinationName = topic;
client.send(Message);
//document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";
}
function send_rx(){
    msg = document.getElementById("frequencyrx").value.toString();
    topic = "/frequency/rx"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";
}
function send_tx(){
    msg = document.getElementById("frequencytx").value.toString();
    topic = "/frequency/tx"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";
}
function send_paris_speed(){
    msg = document.getElementById("paris_speed").value.toString();
    topic = "/paris_speed"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";
}
function send_refresh(){
    msg = "";
    topic = "/refresh"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";

}
function send_tone_freq(){
    msg = document.getElementById("tone_freq").value.toString();
    topic = "/sine_tone"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";
}
function send_space(){
    msg = document.getElementById("space").value.toString();
    topic = "/space_btw"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br>";
}
function send_message(){
    msg = document.getElementById("my_message").value.toString().toUpperCase();
    topic = "/message"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br> "+msg;
}
function send_message_txt(){
    var el = document.getElementById('load_text_here');
    msg = (el.innerText || el.textContent);
    topic = "/message"
    Message = new Paho.Message(msg);
    Message.destinationName = topic;
    client.send(Message);
    //document.getElementById("messages").innerHTML += "<span> Message to topic "+topic+" is sent </span><br> "+msg;
}
function load_message_file(){
    var fileToLoad = document.getElementById("message_file").files[0];
    
    var fileReader = new FileReader();
    fileReader.onload = function(fileLoadedEvent)
        {
        var textFromFileLoaded = fileLoadedEvent.target.result;
        if(textFromFileLoaded.length>500)
        {
            textFromFileLoaded = textFromFileLoaded.substring(0,500);
        }
        document.getElementById("load_text_here").innerHTML = textFromFileLoaded.toUpperCase();
        };
  
    fileReader.readAsText(fileToLoad, "ASCII");

    
}
