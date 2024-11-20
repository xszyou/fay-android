package com.whispertflite.entity;

public class Message {
    private String type;
    private String way;
    private String content;
    private double createtime;
    private String timetext;
    private String username;
    private int id;
    private int isAdopted;

    // Getters and setters
    public String getType() { return type; }
    public void setType(String type) { this.type = type; }

    public String getWay() { return way; }
    public void setWay(String way) { this.way = way; }

    public String getContent() { return content; }
    public void setContent(String content) { this.content = content; }

    public double getCreatetime() { return createtime; }
    public void setCreatetime(double createtime) { this.createtime = createtime; }

    public String getTimetext() { return timetext; }
    public void setTimetext(String timetext) { this.timetext = timetext; }

    public String getUsername() { return username; }
    public void setUsername(String username) { this.username = username; }

    public int getId() { return id; }
    public void setId(int id) { this.id = id; }

    public int getIsAdopted() { return isAdopted; }
    public void setIsAdopted(int isAdopted) { this.isAdopted = isAdopted; }
}
