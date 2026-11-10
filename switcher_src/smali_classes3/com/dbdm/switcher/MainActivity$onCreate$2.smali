.class final synthetic Lcom/dbdm/switcher/MainActivity$onCreate$2;
.super Lkotlin/jvm/internal/FunctionReferenceImpl;
.source "MainActivity.kt"

# interfaces
.implements Lkotlin/jvm/functions/Function1;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/dbdm/switcher/MainActivity;->onCreate(Landroid/os/Bundle;)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x1000
    name = null
.end annotation

.annotation system Ldalvik/annotation/Signature;
    value = {
        "Lkotlin/jvm/internal/FunctionReferenceImpl;",
        "Lkotlin/jvm/functions/Function1<",
        "Lcom/dbdm/switcher/AccountProfile;",
        "Lkotlin/Unit;",
        ">;"
    }
.end annotation

.annotation runtime Lkotlin/Metadata;
    k = 0x3
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation


# direct methods
.method constructor <init>(Ljava/lang/Object;)V
    .locals 7

    const/4 v1, 0x1

    const-class v3, Lcom/dbdm/switcher/MainActivity;

    const-string v4, "onDelete"

    const-string v5, "onDelete(Lcom/dbdm/switcher/AccountProfile;)V"

    const/4 v6, 0x0

    move-object v0, p0

    move-object v2, p1

    invoke-direct/range {v0 .. v6}, Lkotlin/jvm/internal/FunctionReferenceImpl;-><init>(ILjava/lang/Object;Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;I)V

    return-void
.end method


# virtual methods
.method public bridge synthetic invoke(Ljava/lang/Object;)Ljava/lang/Object;
    .locals 1
    .param p1, "p1"    # Ljava/lang/Object;

    .line 57
    move-object v0, p1

    check-cast v0, Lcom/dbdm/switcher/AccountProfile;

    invoke-virtual {p0, v0}, Lcom/dbdm/switcher/MainActivity$onCreate$2;->invoke(Lcom/dbdm/switcher/AccountProfile;)V

    sget-object v0, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    return-object v0
.end method

.method public final invoke(Lcom/dbdm/switcher/AccountProfile;)V
    .locals 1
    .param p1, "p0"    # Lcom/dbdm/switcher/AccountProfile;

    const-string v0, "p0"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 57
    iget-object v0, p0, Lcom/dbdm/switcher/MainActivity$onCreate$2;->receiver:Ljava/lang/Object;

    check-cast v0, Lcom/dbdm/switcher/MainActivity;

    invoke-static {v0, p1}, Lcom/dbdm/switcher/MainActivity;->access$onDelete(Lcom/dbdm/switcher/MainActivity;Lcom/dbdm/switcher/AccountProfile;)V

    return-void
.end method
