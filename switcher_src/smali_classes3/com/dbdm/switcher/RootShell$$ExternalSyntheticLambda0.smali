.class public final synthetic Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;
.super Ljava/lang/Object;
.source "D8$$SyntheticClass"

# interfaces
.implements Ljava/util/concurrent/Callable;


# instance fields
.field public final synthetic f$0:Lcom/dbdm/switcher/RootShell;

.field public final synthetic f$1:Ljava/lang/String;


# direct methods
.method public synthetic constructor <init>(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;)V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    iput-object p1, p0, Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;->f$0:Lcom/dbdm/switcher/RootShell;

    iput-object p2, p0, Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;->f$1:Ljava/lang/String;

    return-void
.end method


# virtual methods
.method public final call()Ljava/lang/Object;
    .locals 2

    iget-object v0, p0, Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;->f$0:Lcom/dbdm/switcher/RootShell;

    iget-object v1, p0, Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;->f$1:Ljava/lang/String;

    invoke-static {v0, v1}, Lcom/dbdm/switcher/RootShell;->$r8$lambda$wYJ8HJOI1muU2SR8Jv3t2qrt4Og(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;)Ljava/lang/String;

    move-result-object v0

    return-object v0
.end method
