.class public final Lcom/dbdm/switcher/ProfileAdapter$VH;
.super Landroidx/recyclerview/widget/RecyclerView$ViewHolder;
.source "ProfileAdapter.kt"


# annotations
.annotation system Ldalvik/annotation/EnclosingClass;
    value = Lcom/dbdm/switcher/ProfileAdapter;
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x11
    name = "VH"
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000\"\n\u0002\u0018\u0002\n\u0002\u0018\u0002\n\u0000\n\u0002\u0018\u0002\n\u0002\u0008\u0002\n\u0002\u0018\u0002\n\u0002\u0008\u0005\n\u0002\u0018\u0002\n\u0002\u0008\t\u0008\u0086\u0004\u0018\u00002\u00020\u0001B\r\u0012\u0006\u0010\u0002\u001a\u00020\u0003\u00a2\u0006\u0002\u0010\u0004R\u0011\u0010\u0005\u001a\u00020\u0006\u00a2\u0006\u0008\n\u0000\u001a\u0004\u0008\u0007\u0010\u0008R\u0011\u0010\t\u001a\u00020\u0006\u00a2\u0006\u0008\n\u0000\u001a\u0004\u0008\n\u0010\u0008R\u0011\u0010\u000b\u001a\u00020\u000c\u00a2\u0006\u0008\n\u0000\u001a\u0004\u0008\r\u0010\u000eR\u0011\u0010\u000f\u001a\u00020\u000c\u00a2\u0006\u0008\n\u0000\u001a\u0004\u0008\u0010\u0010\u000eR\u0011\u0010\u0011\u001a\u00020\u000c\u00a2\u0006\u0008\n\u0000\u001a\u0004\u0008\u0012\u0010\u000eR\u0011\u0010\u0013\u001a\u00020\u000c\u00a2\u0006\u0008\n\u0000\u001a\u0004\u0008\u0014\u0010\u000e\u00a8\u0006\u0015"
    }
    d2 = {
        "Lcom/dbdm/switcher/ProfileAdapter$VH;",
        "Landroidx/recyclerview/widget/RecyclerView$ViewHolder;",
        "v",
        "Landroid/view/View;",
        "(Lcom/dbdm/switcher/ProfileAdapter;Landroid/view/View;)V",
        "btnActivate",
        "Landroid/widget/Button;",
        "getBtnActivate",
        "()Landroid/widget/Button;",
        "btnDelete",
        "getBtnDelete",
        "tvCreated",
        "Landroid/widget/TextView;",
        "getTvCreated",
        "()Landroid/widget/TextView;",
        "tvName",
        "getTvName",
        "tvSsaid",
        "getTvSsaid",
        "tvUserId",
        "getTvUserId",
        "app_debug"
    }
    k = 0x1
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation


# instance fields
.field private final btnActivate:Landroid/widget/Button;

.field private final btnDelete:Landroid/widget/Button;

.field final synthetic this$0:Lcom/dbdm/switcher/ProfileAdapter;

.field private final tvCreated:Landroid/widget/TextView;

.field private final tvName:Landroid/widget/TextView;

.field private final tvSsaid:Landroid/widget/TextView;

.field private final tvUserId:Landroid/widget/TextView;


# direct methods
.method public constructor <init>(Lcom/dbdm/switcher/ProfileAdapter;Landroid/view/View;)V
    .locals 2
    .param p1, "this$0"    # Lcom/dbdm/switcher/ProfileAdapter;
    .param p2, "v"    # Landroid/view/View;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Landroid/view/View;",
            ")V"
        }
    .end annotation

    const-string v0, "v"

    invoke-static {p2, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 19
    iput-object p1, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->this$0:Lcom/dbdm/switcher/ProfileAdapter;

    invoke-direct {p0, p2}, Landroidx/recyclerview/widget/RecyclerView$ViewHolder;-><init>(Landroid/view/View;)V

    .line 20
    sget v0, Lcom/dbdm/switcher/R$id;->tv_name:I

    invoke-virtual {p2, v0}, Landroid/view/View;->findViewById(I)Landroid/view/View;

    move-result-object v0

    const-string v1, "findViewById(...)"

    invoke-static {v0, v1}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v0, Landroid/widget/TextView;

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvName:Landroid/widget/TextView;

    .line 21
    sget v0, Lcom/dbdm/switcher/R$id;->tv_ssaid:I

    invoke-virtual {p2, v0}, Landroid/view/View;->findViewById(I)Landroid/view/View;

    move-result-object v0

    invoke-static {v0, v1}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v0, Landroid/widget/TextView;

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvSsaid:Landroid/widget/TextView;

    .line 22
    sget v0, Lcom/dbdm/switcher/R$id;->tv_user_id:I

    invoke-virtual {p2, v0}, Landroid/view/View;->findViewById(I)Landroid/view/View;

    move-result-object v0

    invoke-static {v0, v1}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v0, Landroid/widget/TextView;

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvUserId:Landroid/widget/TextView;

    .line 23
    sget v0, Lcom/dbdm/switcher/R$id;->tv_created:I

    invoke-virtual {p2, v0}, Landroid/view/View;->findViewById(I)Landroid/view/View;

    move-result-object v0

    invoke-static {v0, v1}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v0, Landroid/widget/TextView;

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvCreated:Landroid/widget/TextView;

    .line 24
    sget v0, Lcom/dbdm/switcher/R$id;->btn_activate:I

    invoke-virtual {p2, v0}, Landroid/view/View;->findViewById(I)Landroid/view/View;

    move-result-object v0

    invoke-static {v0, v1}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v0, Landroid/widget/Button;

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->btnActivate:Landroid/widget/Button;

    .line 25
    sget v0, Lcom/dbdm/switcher/R$id;->btn_delete:I

    invoke-virtual {p2, v0}, Landroid/view/View;->findViewById(I)Landroid/view/View;

    move-result-object v0

    invoke-static {v0, v1}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v0, Landroid/widget/Button;

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->btnDelete:Landroid/widget/Button;

    .line 19
    return-void
.end method


# virtual methods
.method public final getBtnActivate()Landroid/widget/Button;
    .locals 1

    .line 24
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->btnActivate:Landroid/widget/Button;

    return-object v0
.end method

.method public final getBtnDelete()Landroid/widget/Button;
    .locals 1

    .line 25
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->btnDelete:Landroid/widget/Button;

    return-object v0
.end method

.method public final getTvCreated()Landroid/widget/TextView;
    .locals 1

    .line 23
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvCreated:Landroid/widget/TextView;

    return-object v0
.end method

.method public final getTvName()Landroid/widget/TextView;
    .locals 1

    .line 20
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvName:Landroid/widget/TextView;

    return-object v0
.end method

.method public final getTvSsaid()Landroid/widget/TextView;
    .locals 1

    .line 21
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvSsaid:Landroid/widget/TextView;

    return-object v0
.end method

.method public final getTvUserId()Landroid/widget/TextView;
    .locals 1

    .line 22
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter$VH;->tvUserId:Landroid/widget/TextView;

    return-object v0
.end method
